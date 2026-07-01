// mm3d_model — see mm3d_model.h. MM's model-substitution provider.
//
// Structure mirrors OoT's zelda3d_model.cpp but stays minimal: a modelId -> {MM3D archive,
// CMB, scale} table, a lazy geometry cache, and the renderer provider callback. The
// heavy lifting (CMB parse, CMB -> renderer groups + textures) is the SHARED cmb3d code.
#include "mm3d_model.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fast/zelda3d_gl.h> // renderer contract + Zelda3D_GL_SetModelProvider

#include "asset/cmb.h"
#include "asset/cmb_glgroups.h" // shared Zelda3D::MakeGlGroup / AppendCmbTextures
#include "asset/ctr_rom.h"
#include "asset/gar.h" // MM3D actor archives are Grezzo GAR2, not OoT3D ZAR

namespace {

// Which MM3D archive/CMB replaces a given renderer model id, and its world scale.
// MM3D actor models live in Grezzo GAR2 archives at /actors/zelda2_<name>.gar.lzs
// (uncompressed despite the .lzs extension; LzS-compressed archives come later). The
// preferred .cmb inside the archive is selected by name, else the first .cmb.
struct ModelSpec {
    const char* garPath;  // RomFS virtual path, e.g. "/actors/zelda2_xxx.gar.lzs"
    const char* cmbName;  // preferred .cmb inside the GAR (nullptr = first .cmb)
    float worldScale;     // N64 world units per CMB unit
};
const ModelSpec kModels[] = {
    // slot 0: Obj_Tokei_Step (Clock Tower door/wall) — OBJECT_TOKEI_STEP (0x1A4). Up high
    // on the tower; may not draw from the ground. N64 actor->scale 0.1.
    { "/actors/zelda2_tokei_step.gar.lzs", nullptr, 0.1f },
    // slot 1: Obj_Tokei_Tobira (Clock Tower Swinging Doors) — OBJECT_TOKEI_TOBIRA (0x197).
    // Ground-level, in view at the South Clock Town start (the first reliably-drawn target).
    // N64 actor draws its DL at actor->scale 0.1; MM3D CMB (z2_tobira_h, ~1400 local) draws
    // directly in world units, so first-guess world scale = 0.1. Calibrated live via `mscale`
    // against the Azahar 3DS oracle; auto-measure (OoT's G_ZELDA3D_MEASURE path) comes later.
    { "/actors/zelda2_tokei_tobira.gar.lzs", nullptr, 0.1f },
};
constexpr int kModelCount = (int)(sizeof(kModels) / sizeof(kModels[0]));

// Lazily-loaded geometry; owns the CPU arrays the renderer reads during upload.
struct Loaded {
    std::unique_ptr<Zelda3D::Cmb> cmb;
    std::vector<Zelda3D::CmbDrawGroup> groups; // owns verts (cGroups alias into these)
    std::vector<std::vector<uint8_t>> texRgba;
    std::vector<Zelda3DGlGroup> cGroups;
    std::vector<Zelda3DGlTex> cTexs;
    bool ok = false;
};
std::unordered_map<int, std::unique_ptr<Loaded>> g_loaded;

// The decrypted MM3D ROM (NCSD->NCCH->RomFS), opened once from ZELDA3D_MM3D_ROM.
std::unique_ptr<Zelda3D::CtrRom> g_rom;
bool g_romTried = false;
Zelda3D::CtrRom* rom() {
    if (!g_romTried) {
        g_romTried = true;
        const char* path = getenv("ZELDA3D_MM3D_ROM");
        if (!path) {
            fprintf(stderr, "[MM3D] ZELDA3D_MM3D_ROM not set — cannot load MM3D assets\n");
            return nullptr;
        }
        g_rom = std::make_unique<Zelda3D::CtrRom>(path);
        if (!g_rom->ok()) {
            fprintf(stderr, "[MM3D] CtrRom(%s): %s\n", path, g_rom->error().c_str());
            g_rom.reset();
            return nullptr;
        }
    }
    return g_rom.get();
}

Loaded* loadModel(int modelId) {
    auto it = g_loaded.find(modelId);
    if (it != g_loaded.end()) {
        return it->second.get();
    }
    auto owned = std::make_unique<Loaded>();
    Loaded* out = owned.get();
    g_loaded[modelId] = std::move(owned);

    if (modelId < 0 || modelId >= kModelCount) {
        return out;
    }
    const ModelSpec& spec = kModels[modelId];
    if (spec.garPath == nullptr) {
        return out;
    }
    Zelda3D::CtrRom* r = rom();
    if (r == nullptr) {
        return out;
    }
    std::vector<uint8_t> garBytes = r->read(spec.garPath);
    if (garBytes.empty()) {
        fprintf(stderr, "[MM3D] gar not found: %s\n", spec.garPath);
        return out;
    }
    Zelda3D::Gar gar(std::move(garBytes));
    if (!gar.ok()) {
        fprintf(stderr, "[MM3D] gar parse %s: %s\n", spec.garPath, gar.error().c_str());
        return out;
    }
    const Zelda3D::GarFile* cmbFile = nullptr;
    if (spec.cmbName != nullptr) {
        for (const auto& f : gar.files()) {
            if (f.name == spec.cmbName) {
                cmbFile = &f;
                break;
            }
        }
    }
    if (cmbFile == nullptr) {
        cmbFile = gar.firstWithSuffix(".cmb");
    }
    if (cmbFile == nullptr) {
        fprintf(stderr, "[MM3D] no .cmb in %s\n", spec.garPath);
        return out;
    }
    out->cmb = std::make_unique<Zelda3D::Cmb>(gar.read(*cmbFile));
    if (!out->cmb->ok()) {
        fprintf(stderr, "[MM3D] cmb parse %s: %s\n", spec.garPath, out->cmb->error().c_str());
        return out;
    }
    out->groups = out->cmb->buildDrawGroups();
    // Characters/props are lit dynamically; their CMB vertex-color attribute is unused
    // (would render black), so force white — matches OoT's non-scene-room path.
    for (auto& g : out->groups) {
        for (auto& v : g.verts) {
            v.color[0] = v.color[1] = v.color[2] = v.color[3] = 1.0f;
        }
    }
    std::vector<std::pair<int, int>> dims;
    Zelda3D::AppendCmbTextures(*out->cmb, out->texRgba, dims);
    out->cTexs.resize(out->texRgba.size());
    for (size_t i = 0; i < out->texRgba.size(); i++) {
        out->cTexs[i] = { out->texRgba[i].data(), dims[i].first, dims[i].second };
    }
    out->cGroups.reserve(out->groups.size());
    for (const auto& g : out->groups) {
        out->cGroups.push_back(Zelda3D::MakeGlGroup(*out->cmb, g, g.verts.data(), 0));
    }
    out->ok = true;
    fprintf(stderr, "[MM3D] loaded model %d (%s): %zu groups, %zu textures\n", modelId, spec.garPath,
            out->cGroups.size(), out->cTexs.size());
    return out;
}

// Renderer provider: hand back a model's CPU data (loads lazily on first draw).
int provider(int modelId, const Zelda3DGlGroup** groups, int* groupCount, const Zelda3DGlTex** texs, int* texCount) {
    Loaded* lm = loadModel(modelId);
    if (lm == nullptr || !lm->ok || lm->cGroups.empty()) {
        return 0;
    }
    *groups = lm->cGroups.data();
    *groupCount = (int)lm->cGroups.size();
    *texs = lm->cTexs.data();
    *texCount = (int)lm->cTexs.size();
    return 1;
}

bool g_registered = false;

} // namespace

extern "C" {

void MM3D_EnsureModelProvider(void) {
    if (!g_registered) {
        Zelda3D_GL_SetModelProvider(provider);
        g_registered = true;
    }
}

// Live scale override for the model under calibration (set via the `mscale` REPL command).
// -1 = use the table value. A single knob is enough while one prop is being dialed in.
float g_scaleOverride = -1.0f;

int MM3D_LookupModel(int actorId, int objectId, int* modelId, float* worldScale, float* groundOffset) {
    // Map an actor's loaded N64 object id (preferred: it names the asset) to a model slot.
    // Faithful-first: exactly one entry until the first prop is verified.
    (void)actorId;
    int id = -1;
    switch (objectId) {
        case 0x1A4: id = 0; break; // OBJECT_TOKEI_STEP   -> slot 0
        case 0x197: id = 1; break; // OBJECT_TOKEI_TOBIRA -> slot 1
        default: return 0;
    }
    if (modelId != nullptr) {
        *modelId = id;
    }
    if (worldScale != nullptr) {
        *worldScale = (g_scaleOverride > 0.0f) ? g_scaleOverride : kModels[id].worldScale;
    }
    if (groundOffset != nullptr) {
        *groundOffset = 0.0f;
    }
    return 1;
}

float MM3D_ModelScaleById(int modelId) {
    if (modelId < 0 || modelId >= kModelCount) {
        return 1.0f;
    }
    return kModels[modelId].worldScale;
}

void MM3D_SetScaleOverride(float scale) {
    g_scaleOverride = scale;
}

} // extern "C"
