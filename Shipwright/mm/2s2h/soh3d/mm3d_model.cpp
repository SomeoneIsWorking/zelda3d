// mm3d_model — see mm3d_model.h. MM's model-substitution provider.
//
// Structure mirrors OoT's soh3d_model.cpp but stays minimal: a modelId -> {MM3D archive,
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

#include <fast/soh3d_gl.h> // renderer contract + SoH3D_GL_SetModelProvider

#include "asset/cmb.h"
#include "asset/cmb_glgroups.h" // shared SoH3D::MakeGlGroup / AppendCmbTextures
#include "asset/ctr_rom.h"
#include "asset/zar.h"

namespace {

// Which MM3D archive/CMB replaces a given renderer model id, and its world scale.
// EMPTY for now (single reserved slot so sizeof()/indexing stay valid): until the MM3D
// RomFS archive names are inventoried (N4 step 2b), MM3D_LookupModel matches nothing and
// the whole path is inert — MM renders vanilla N64. Faithful-first: one verified prop
// entry lands here before any auto table.
struct ModelSpec {
    const char* zarPath;  // RomFS virtual path, e.g. "/actor/zelda_xxx.zar"
    const char* cmbName;  // preferred .cmb inside the ZAR (nullptr = first .cmb)
    float worldScale;     // N64 world units per CMB unit
};
const ModelSpec kModels[] = {
    { nullptr, nullptr, 1.0f }, // slot 0: reserved / no model
};
constexpr int kModelCount = (int)(sizeof(kModels) / sizeof(kModels[0]));

// Lazily-loaded geometry; owns the CPU arrays the renderer reads during upload.
struct Loaded {
    std::unique_ptr<SoH3D::Cmb> cmb;
    std::vector<SoH3D::CmbDrawGroup> groups; // owns verts (cGroups alias into these)
    std::vector<std::vector<uint8_t>> texRgba;
    std::vector<SoH3DGlGroup> cGroups;
    std::vector<SoH3DGlTex> cTexs;
    bool ok = false;
};
std::unordered_map<int, std::unique_ptr<Loaded>> g_loaded;

// The decrypted MM3D ROM (NCSD->NCCH->RomFS), opened once from ZELDA3D_MM_3DS_ROM.
std::unique_ptr<SoH3D::CtrRom> g_rom;
bool g_romTried = false;
SoH3D::CtrRom* rom() {
    if (!g_romTried) {
        g_romTried = true;
        const char* path = getenv("ZELDA3D_MM_3DS_ROM");
        if (!path) {
            fprintf(stderr, "[MM3D] ZELDA3D_MM_3DS_ROM not set — cannot load MM3D assets\n");
            return nullptr;
        }
        g_rom = std::make_unique<SoH3D::CtrRom>(path);
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
    if (spec.zarPath == nullptr) {
        return out;
    }
    SoH3D::CtrRom* r = rom();
    if (r == nullptr) {
        return out;
    }
    std::vector<uint8_t> zarBytes = r->read(spec.zarPath);
    if (zarBytes.empty()) {
        fprintf(stderr, "[MM3D] zar not found: %s\n", spec.zarPath);
        return out;
    }
    SoH3D::Zar zar(std::move(zarBytes));
    const SoH3D::ZarFile* cmbFile = nullptr;
    if (spec.cmbName != nullptr) {
        for (const auto& f : zar.files()) {
            if (f.name == spec.cmbName) {
                cmbFile = &f;
                break;
            }
        }
    }
    if (cmbFile == nullptr) {
        cmbFile = zar.firstWithSuffix(".cmb");
    }
    if (cmbFile == nullptr) {
        fprintf(stderr, "[MM3D] no .cmb in %s\n", spec.zarPath);
        return out;
    }
    out->cmb = std::make_unique<SoH3D::Cmb>(zar.read(*cmbFile));
    if (!out->cmb->ok()) {
        fprintf(stderr, "[MM3D] cmb parse %s: %s\n", spec.zarPath, out->cmb->error().c_str());
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
    SoH3D::AppendCmbTextures(*out->cmb, out->texRgba, dims);
    out->cTexs.resize(out->texRgba.size());
    for (size_t i = 0; i < out->texRgba.size(); i++) {
        out->cTexs[i] = { out->texRgba[i].data(), dims[i].first, dims[i].second };
    }
    out->cGroups.reserve(out->groups.size());
    for (const auto& g : out->groups) {
        out->cGroups.push_back(SoH3D::MakeGlGroup(*out->cmb, g, g.verts.data(), 0));
    }
    out->ok = true;
    fprintf(stderr, "[MM3D] loaded model %d (%s): %zu groups, %zu textures\n", modelId, spec.zarPath,
            out->cGroups.size(), out->cTexs.size());
    return out;
}

// Renderer provider: hand back a model's CPU data (loads lazily on first draw).
int provider(int modelId, const SoH3DGlGroup** groups, int* groupCount, const SoH3DGlTex** texs, int* texCount) {
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
        SoH3D_GL_SetModelProvider(provider);
        g_registered = true;
    }
}

int MM3D_LookupModel(int actorId, int objectId, int* modelId, float* worldScale, float* groundOffset) {
    // Table is empty (kModels has no real entries yet), so nothing matches and MM draws
    // vanilla N64. Populated in N4 step 2b once MM3D archive names are inventoried.
    (void)actorId;
    (void)objectId;
    (void)modelId;
    (void)worldScale;
    (void)groundOffset;
    return 0;
}

float MM3D_ModelScaleById(int modelId) {
    if (modelId < 0 || modelId >= kModelCount) {
        return 1.0f;
    }
    return kModels[modelId].worldScale;
}

} // extern "C"
