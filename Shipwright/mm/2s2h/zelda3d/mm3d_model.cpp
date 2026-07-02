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
#include "asset/lzs.h" // ~40% of MM3D actor .gar.lzs archives are LzS-wrapped GAR2

namespace {

// Auto-map: MM3D actor archives live at /actors/zelda2_<short-name>.gar.lzs, where the
// short-name matches the object_table.h `object_<short>` symbol. So for every objectId with
// an archive we can dynamically discover the mapping — no per-object hand-list. The generated
// kObjectNames inc gives us objectId -> short-name; we probe the archive once and cache the
// (objectId -> renderer modelId) resolution.
#include "mm3d_object_names.inc"

// One live model slot. Dynamically allocated as objects resolve for the first time.
struct ModelSpec {
    std::string garPath;  // "/actors/zelda2_<name>.gar.lzs"
    float worldScale;     // N64 world units per CMB unit; first-guess 0.1 until calibrated.
};
std::vector<ModelSpec> g_models;
// objectId -> renderer modelId (or -1 if resolution attempted and failed — cache miss).
std::unordered_map<int, int> g_objectToModel;

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

    if (modelId < 0 || modelId >= (int)g_models.size()) {
        return out;
    }
    const ModelSpec& spec = g_models[modelId];
    if (spec.garPath.empty()) {
        return out;
    }
    Zelda3D::CtrRom* r = rom();
    if (r == nullptr) {
        return out;
    }
    std::vector<uint8_t> garBytes = r->read(spec.garPath);
    if (garBytes.empty()) {
        fprintf(stderr, "[MM3D] gar not found: %s\n", spec.garPath.c_str());
        return out;
    }
    if (Zelda3D::LzsIsCompressed(garBytes)) {
        std::string lzsErr;
        std::vector<uint8_t> inflated = Zelda3D::LzsDecompress(garBytes, &lzsErr);
        if (inflated.empty()) {
            fprintf(stderr, "[MM3D] LzS inflate %s: %s\n", spec.garPath.c_str(), lzsErr.c_str());
            return out;
        }
        garBytes = std::move(inflated);
    }
    Zelda3D::Gar gar(std::move(garBytes));
    if (!gar.ok()) {
        fprintf(stderr, "[MM3D] gar parse %s: %s\n", spec.garPath.c_str(), gar.error().c_str());
        return out;
    }
    const Zelda3D::GarFile* cmbFile = gar.firstWithSuffix(".cmb");
    if (cmbFile == nullptr) {
        fprintf(stderr, "[MM3D] no .cmb in %s\n", spec.garPath.c_str());
        return out;
    }
    out->cmb = std::make_unique<Zelda3D::Cmb>(gar.read(*cmbFile));
    if (!out->cmb->ok()) {
        fprintf(stderr, "[MM3D] cmb parse %s: %s\n", spec.garPath.c_str(), out->cmb->error().c_str());
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
    fprintf(stderr, "[MM3D] loaded model %d (%s): %zu groups, %zu textures\n", modelId, spec.garPath.c_str(),
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

void Zelda3D_EnsureModelProvider(void) {
    if (!g_registered) {
        Zelda3D_GL_SetModelProvider(provider);
        g_registered = true;
    }
}

// Live scale override for the model under calibration (set via the `mscale` REPL command).
// -1 = use the table value. A single knob is enough while one prop is being dialed in.
float g_scaleOverride = -1.0f;

// Look up the object short-name from the generated kObjectNames table (objectId ordered).
static const char* objectShortName(int objectId) {
    int lo = 0, hi = (int)(sizeof(kObjectNames) / sizeof(kObjectNames[0])) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (kObjectNames[mid].id == objectId) return kObjectNames[mid].name;
        if (kObjectNames[mid].id < objectId) lo = mid + 1; else hi = mid - 1;
    }
    return nullptr;
}

// Resolve an objectId -> renderer modelId. Probe the MM3D archive once per object; only
// commit archives whose CMB is safe for the STATIC draw path (<=1 bone — no CSAB rig
// needed). Skinned actors (bones > 1) return -1 so the caller falls through to vanilla
// N64 draw until the SkelAnime replacement path is wired for MM. This supersedes the
// old hardcoded allow-list gate (kept as a fast-lane "known-good, first-guess scale"
// pass-through — everything else is auto-probed).
static int resolveModelForObject(int objectId) {
    auto it = g_objectToModel.find(objectId);
    if (it != g_objectToModel.end()) return it->second;

    const char* name = objectShortName(objectId);
    Zelda3D::CtrRom* r = rom();
    if (name == nullptr || r == nullptr) { g_objectToModel[objectId] = -1; return -1; }

    std::string path = std::string("/actors/zelda2_") + name + ".gar.lzs";
    std::vector<uint8_t> bytes = r->read(path);
    if (bytes.empty()) { g_objectToModel[objectId] = -1; return -1; }
    // MM3D actor archives that carry the .gar.lzs extension come in two flavours:
    // raw GAR2, or a GAR2 payload wrapped in Grezzo's LzS ("LzS\1") LZSS format.
    // Auto-detect and inflate the LzS ones in place; then the downstream GAR2 parse
    // handles both uniformly.
    if (Zelda3D::LzsIsCompressed(bytes)) {
        std::string lzsErr;
        std::vector<uint8_t> inflated = Zelda3D::LzsDecompress(bytes, &lzsErr);
        if (inflated.empty()) {
            fprintf(stderr, "[MM3D] LzS inflate failed for %s: %s\n", path.c_str(), lzsErr.c_str());
            g_objectToModel[objectId] = -1;
            return -1;
        }
        bytes = std::move(inflated);
    }
    if (bytes.size() < 4 || memcmp(bytes.data(), "GAR\x02", 4) != 0) {
        g_objectToModel[objectId] = -1;
        return -1;
    }

    // Auto-probe: parse the GAR2 header + first CMB, reject if the CMB is skinned.
    // The static draw path can't pose rigged models; drawing them would either T-pose
    // or crash. Anything skinned falls through to N64 until the SkelAnime replacement
    // path lands for MM (see mm3d_model.cpp header comment).
    std::vector<uint8_t> probeBytes = bytes;  // copy — the lazy Load re-reads from ROM
    Zelda3D::Gar probe(std::move(probeBytes));
    if (!probe.ok()) {
        g_objectToModel[objectId] = -1;
        return -1;
    }
    const Zelda3D::GarFile* cmbFile = probe.firstWithSuffix(".cmb");
    if (cmbFile == nullptr) {
        g_objectToModel[objectId] = -1;
        return -1;
    }
    Zelda3D::Cmb probeCmb(probe.read(*cmbFile));
    if (!probeCmb.ok()) {
        fprintf(stderr, "[MM3D] cmb probe %s: %s\n", path.c_str(), probeCmb.error().c_str());
        g_objectToModel[objectId] = -1;
        return -1;
    }
    if (probeCmb.bones().size() > 1) {
        // Skinned — needs the animated-actor path; not wired for MM yet.
        g_objectToModel[objectId] = -1;
        return -1;
    }

    int newId = (int)g_models.size();
    g_models.push_back({ path, 0.1f });
    g_objectToModel[objectId] = newId;
    fprintf(stderr, "[MM3D] mapped obj=0x%03X (%s) -> modelId=%d (rigid, %zu bones)\n",
            objectId, name, newId, probeCmb.bones().size());
    return newId;
}

int Zelda3D_LookupModel(int actorId, int objectId, int* modelId, float* worldScale, float* groundOffset) {
    (void)actorId;
    int id = resolveModelForObject(objectId);
    if (id < 0) return 0;
    if (modelId != nullptr) *modelId = id;
    if (worldScale != nullptr) *worldScale = (g_scaleOverride > 0.0f) ? g_scaleOverride : g_models[id].worldScale;
    if (groundOffset != nullptr) *groundOffset = 0.0f;
    return 1;
}

float Zelda3D_ModelScaleById(int modelId) {
    if (modelId < 0 || modelId >= (int)g_models.size()) return 1.0f;
    return g_models[modelId].worldScale;
}

void Zelda3D_SetScaleOverride(float scale) {
    g_scaleOverride = scale;
}

} // extern "C"
