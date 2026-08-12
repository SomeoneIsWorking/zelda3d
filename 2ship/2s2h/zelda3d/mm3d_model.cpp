// mm3d_model — see mm3d_model.h. MM's model-substitution provider.
//
// Structure mirrors OoT's zelda3d_model.cpp but stays minimal: a modelId -> {MM3D archive,
// CMB, scale} table, a lazy geometry cache, and the renderer provider callback. The
// heavy lifting (CMB parse, CMB -> renderer groups + textures) is the SHARED cmb3d code.
#include "mm3d_model.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fast/zelda3d_gl.h> // renderer contract + Zelda3D_GL_SetModelProvider

#include "asset/cmb.h"
#include "asset/zsi.h"
#include "asset/cmb_glgroups.h" // shared Zelda3D::MakeGlGroup / AppendCmbTextures
#include "asset/csab.h" // shared 3DS CSAB sampler — plays the MM3D actor's OWN animations
#include "asset/ctr_rom.h"
#include "asset/gar.h" // MM3D actor archives are Grezzo GAR2, not OoT3D ZAR
#include "asset/lzs.h" // ~40% of MM3D actor .gar.lzs archives are LzS-wrapped GAR2

#include <array>
#include <cmath>
#include <set>

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
    bool skinned = false; // true = draw via SkelAnime intercept (3DS CSAB anim on the 3DS rig)
};
std::vector<ModelSpec> g_models;

// Scene-room model id space. Room ids live ABOVE every actor model id so the two never collide
// (actor ids are plain indices into g_models, which grows at runtime). Mirrors OoT's
// kSceneModelBase in soh/src/zelda3d/model/zelda3d_model.cpp.
constexpr int kMmSceneModelBase = 1000000;
std::vector<std::string> g_sceneRoomPaths;              // index = modelId - kMmSceneModelBase
std::unordered_map<std::string, int> g_sceneRoomIds;    // zsi path -> modelId
// objectId -> renderer modelId (or -1 if resolution attempted and failed — cache miss).
std::unordered_map<int, int> g_objectToModel;
// Pending per-object scale overrides set by REPL BEFORE the object has been auto-probed.
// Applied to ModelSpec.worldScale when resolveModelForObject() first commits the mapping.
std::unordered_map<int, float> g_pendingScale;
// Object short-name (for `mlist` output) — populated at map time to avoid
// re-searching kObjectNames on every dump. Keyed by objectId.
std::unordered_map<int, std::string> g_objectName;

// Lazily-loaded geometry; owns the CPU arrays the renderer reads during upload.
struct Loaded {
    std::unique_ptr<Zelda3D::Gar> gar; // kept alive so CSAB anims can be read lazily
    std::unique_ptr<Zelda3D::Cmb> cmb;
    std::vector<Zelda3D::CmbDrawGroup> groups; // owns verts (cGroups alias into these)
    std::vector<std::vector<uint8_t>> texRgba;
    // Per-texture mip level count, as required by AppendCmbTextures. MM only uploads the BASE level,
    // which is the front of each texRgba entry (levels are stored largest-first, back-to-back), so a
    // multi-level entry is correctly read as its base image and the extra bytes are simply unused.
    // Kept as a real field rather than a throwaway local so wiring MM's uploader to the mip chain
    // later is a one-line change instead of a signature change.
    std::vector<int> texLevels;
    std::vector<Zelda3DGlGroup> cGroups;
    std::vector<Zelda3DGlTex> cTexs;
    bool ok = false;
    // Parsed CSAB cache, keyed by base name ("dog_wait"). The MM3D actor's OWN 3DS animations,
    // read from the same GAR as the CMB. Mirrors OoT's LoadedModel::anims (zar-sourced there).
    std::unordered_map<std::string, std::unique_ptr<Zelda3D::Csab>> anims;
    // Data-driven default idle CSAB (the *_wait clip, else the first CSAB) — the fallback when
    // this actor's live N64 animation isn't in the N64->3DS map, so an unmapped state reads as
    // standing rather than freezing at bind pose. Resolved once from the GAR's file list.
    std::string defaultAnim;
    bool defaultAnimResolved = false;
    // GAR basename ("zelda2_dog"), the key of the generated default-idle table.
    std::string garName;
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

// Load a scene-room model: read its ZSI, take the single embedded room CMB and build draw groups.
// No skeleton/animation — scene verts are already WORLD-space, so it draws at the origin.
// Unlike the actor path below, the CMB's vertex colour is KEPT: scene rooms carry MM3D's baked
// vertex lighting/AO, which is exactly what makes them look right unlit.
static void loadSceneRoom(int modelId, Loaded* out) {
    int idx = modelId - kMmSceneModelBase;
    if (idx < 0 || idx >= (int)g_sceneRoomPaths.size()) {
        return;
    }
    const std::string& path = g_sceneRoomPaths[idx];
    Zelda3D::CtrRom* r = rom();
    if (r == nullptr) {
        return;
    }
    auto bytes = r->read(path);
    if (bytes.empty()) {
        fprintf(stderr, "[MM3D] zsi not found: %s\n", path.c_str());
        return;
    }
    // MM3D stores its scene ZSIs LzS-COMPRESSED ("LzS\x01"), unlike OoT3D which stores them raw
    // ("ZSI\x01") — same codec the actor GARs use, so inflate before parsing or the shared parser
    // (correctly) reports "bad ZSI magic".
    if (Zelda3D::LzsIsCompressed(bytes)) {
        std::string lzsErr;
        std::vector<uint8_t> inflated = Zelda3D::LzsDecompress(bytes, &lzsErr);
        if (inflated.empty()) {
            fprintf(stderr, "[MM3D] LzS inflate %s: %s\n", path.c_str(), lzsErr.c_str());
            return;
        }
        bytes = std::move(inflated);
    }
    Zelda3D::Zsi zsi(std::move(bytes));
    if (!zsi.ok()) {
        fprintf(stderr, "[MM3D] Zsi %s: %s\n", path.c_str(), zsi.error().c_str());
        return;
    }
    if (!zsi.hasGeometry()) {
        fprintf(stderr, "[MM3D] no room geometry in %s\n", path.c_str());
        return;
    }
    out->cmb = std::make_unique<Zelda3D::Cmb>(zsi.cmbBytes());
    if (!out->cmb->ok()) {
        fprintf(stderr, "[MM3D] Cmb %s: %s\n", path.c_str(), out->cmb->error().c_str());
        return;
    }
    out->groups = out->cmb->buildDrawGroups();
    std::vector<std::pair<int, int>> dims;
    Zelda3D::AppendCmbTextures(*out->cmb, out->texRgba, dims, out->texLevels);
    out->cTexs.resize(out->texRgba.size());
    for (size_t i = 0; i < out->texRgba.size(); i++) {
        out->cTexs[i] = { out->texRgba[i].data(), dims[i].first, dims[i].second };
    }
    out->cGroups.reserve(out->groups.size());
    for (const auto& g : out->groups) {
        out->cGroups.push_back(Zelda3D::MakeGlGroup(*out->cmb, g, g.verts.data(), 0));
    }
    out->ok = true;
    fprintf(stderr, "[MM3D] loaded scene-room model %d (%s): %zu groups, %zu textures\n", modelId,
            path.c_str(), out->cGroups.size(), out->cTexs.size());
}

Loaded* loadModel(int modelId) {
    auto it = g_loaded.find(modelId);
    if (it != g_loaded.end()) {
        return it->second.get();
    }
    auto owned = std::make_unique<Loaded>();
    Loaded* out = owned.get();
    g_loaded[modelId] = std::move(owned);

    if (modelId >= kMmSceneModelBase) {
        loadSceneRoom(modelId, out);
        return out;
    }
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
    {   // "/actors/zelda2_dog.gar.lzs" -> "zelda2_dog"
        size_t slash = spec.garPath.find_last_of('/');
        std::string base = slash == std::string::npos ? spec.garPath : spec.garPath.substr(slash + 1);
        size_t dot = base.find('.');
        out->garName = dot == std::string::npos ? base : base.substr(0, dot);
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
    out->gar = std::make_unique<Zelda3D::Gar>(std::move(garBytes));
    if (!out->gar->ok()) {
        fprintf(stderr, "[MM3D] gar parse %s: %s\n", spec.garPath.c_str(), out->gar->error().c_str());
        return out;
    }
    const Zelda3D::GarFile* cmbFile = out->gar->firstWithSuffix(".cmb");
    if (cmbFile == nullptr) {
        fprintf(stderr, "[MM3D] no .cmb in %s\n", spec.garPath.c_str());
        return out;
    }
    out->cmb = std::make_unique<Zelda3D::Cmb>(out->gar->read(*cmbFile));
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
    Zelda3D::AppendCmbTextures(*out->cmb, out->texRgba, dims, out->texLevels);
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


} // namespace

extern "C" {

// The provider slot lives in libultraship (`Zelda3D_GL_SetModelProvider` -> one `g_provider`), which
// both game cores SHARE, while this latch is a file static inside ONE core. So the latch was making
// a claim it had no standing to make: "the provider is already mine" was only ever "I set it once".
//
// On `mm -> oot -> mm` that is wrong by the third run. MM registers on run 1; OoT overwrites the
// shared slot on run 2; MM's latch is still set on run 3, so it never re-registers and every MM
// model lookup resolves through OoT's provider and OoT's id space. The failure is silent -- wrong
// or missing geometry, not a crash -- which is the kind this codebase has learned to distrust most.
//
// Fixed by deleting the latch rather than making it run-scoped: a run stamp would also work today,
// but only because exactly one core runs at a time, and the store is two pointer writes. "Ensure"
// now means ensure.
void Zelda3D_EnsureModelProvider(void) {
    Zelda3D_GL_SetModelProvider(provider);
}


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
        // Skinned — the SkelAnime intercept (z_skelanime.c choke points -> Zelda3D_MM_InterceptSkelAnime
        // -> SkelAnimeDrawRaw) drives the 3DS CMB rig with the actor's OWN 3DS CSAB animation (resolved
        // from the live N64 anim, phase-locked). Accepting a skinned archive here therefore yields a
        // live-animated MM3D draw from real 3DS motion data. Behind the ZELDA3D_MM_SKINNED env gate
        // pending the N64->3DS anim map growing to cover each actor's states (docs/MM_SKELANIME_PORT.md
        // Stage 4); unmapped states fall back to the model's default idle CSAB.
        static int sSkinnedEnable = -1;
        if (sSkinnedEnable < 0) {
            const char* v = getenv("ZELDA3D_MM_SKINNED");
            sSkinnedEnable = (v != nullptr && v[0] != '\0' && v[0] != '0') ? 1 : 0;
        }
        if (!sSkinnedEnable) {
            fprintf(stderr, "[MM3D] skip obj=0x%03X (%s): skinned (%zu bones)\n",
                    objectId, name, probeCmb.bones().size());
            g_objectToModel[objectId] = -1;
            return -1;
        }
        fprintf(stderr, "[MM3D] skinned obj=0x%03X (%s): %zu bones (3DS CSAB-animated draw)\n",
                objectId, name, probeCmb.bones().size());
    }
    bool isSkinned = probeCmb.bones().size() > 1;

    int newId = (int)g_models.size();
    float initialScale = 0.1f;
    auto pending = g_pendingScale.find(objectId);
    if (pending != g_pendingScale.end() && pending->second > 0.0f) {
        initialScale = pending->second;
    }
    g_models.push_back({ path, initialScale, isSkinned });
    g_objectToModel[objectId] = newId;
    g_objectName[objectId] = name;
    fprintf(stderr, "[MM3D] mapped obj=0x%03X (%s) -> modelId=%d (%s, %zu bones) scale=%.4f\n",
            objectId, name, newId, isSkinned ? "skinned" : "rigid",
            probeCmb.bones().size(), initialScale);
    return newId;
}

int Zelda3D_LookupModel(int actorId, int objectId, int* modelId, float* worldScale, float* groundOffset) {
    (void)actorId;
    int id = resolveModelForObject(objectId);
    if (id < 0) return 0;
    if (modelId != nullptr) *modelId = id;
    if (worldScale != nullptr) *worldScale = g_models[id].worldScale;
    if (groundOffset != nullptr) *groundOffset = 0.0f;
    return 1;
}

float Zelda3D_ModelScaleById(int modelId) {
    if (modelId < 0 || modelId >= (int)g_models.size()) return 1.0f;
    return g_models[modelId].worldScale;
}

// Register (or return) the model id for an MM3D scene room. Geometry loads lazily on first draw.
// MM3D uses /scenes/ (plural); OoT3D uses /scene/. Returns -1 for a null/empty scene name.
int Zelda3D_MM_RoomModelId(const char* sceneName, int roomNum) {
    if (sceneName == nullptr || *sceneName == '\0' || roomNum < 0) {
        return -1;
    }
    std::string path = "/scenes/" + std::string(sceneName) + "_" + std::to_string(roomNum) + "_info.zsi";
    auto it = g_sceneRoomIds.find(path);
    if (it != g_sceneRoomIds.end()) {
        return it->second;
    }
    int id = kMmSceneModelBase + (int)g_sceneRoomPaths.size();
    g_sceneRoomPaths.push_back(path);
    g_sceneRoomIds[path] = id;
    return id;
}

int Zelda3D_IsModelSkinned(int modelId) {
    if (modelId < 0 || modelId >= (int)g_models.size()) return 0;
    return g_models[modelId].skinned ? 1 : 0;
}

void Zelda3D_SetObjectScale(int objectId, float scale) {
    if (scale <= 0.0f) {
        g_pendingScale.erase(objectId);
        // Reset the live model back to the default too so a fresh probe would
        // reproduce the same result. 0.1f matches resolveModelForObject().
        auto it = g_objectToModel.find(objectId);
        if (it != g_objectToModel.end() && it->second >= 0 && it->second < (int)g_models.size()) {
            g_models[it->second].worldScale = 0.1f;
        }
        return;
    }
    g_pendingScale[objectId] = scale;
    auto it = g_objectToModel.find(objectId);
    if (it != g_objectToModel.end() && it->second >= 0 && it->second < (int)g_models.size()) {
        g_models[it->second].worldScale = scale;
    }
}

// STAGE 4 — CSAB anim: skinned MM3D actors play their OWN 3DS animations (the .csab clips
// shipped in the same GAR as the .cmb), NOT a retarget of the N64 pose. This mirrors OoT's
// architecture (Shipwright/soh/src/zelda3d/anim/zelda3d_anim.cpp Zelda3D_UpdateAnim +
// core/zelda3d.c auto path): resolve the actor's live N64 animation -> a 3DS CSAB name via
// the N64->3DS map, sample that CSAB (shared Zelda3D::Csab) phase-locked to the N64 playhead,
// upload the skin matrices. The 3DS rig is posed by its own 3DS motion data.

// Pending state — set by mm3d_draw's TryDrawActor when a skinned model is
// resolved; consumed by SkelAnimeDrawRaw and cleared in AfterActorDraw.
struct MMPending {
    void* actor = nullptr; // MM Actor*
    int modelId = -1;
    float scale = 1.0f;
    float groundOff = 0.0f;
};
MMPending g_pending;

// Live N64 anim state captured in SkelAnime_Update (Zelda3D_MM_CaptureAnimState), keyed by the
// stable jointTable pointer. The draw choke has only jointTable, so this is how it recovers WHICH
// N64 animation is playing + its playhead — the inputs to CSAB selection + phase-lock.
struct MMAnimState {
    const char* animOtr = nullptr; // skelAnime->animation (an OTR path string in 2s2h)
    float curFrame = 0.0f;
    float animLength = 0.0f;
    float morphWeight = 0.0f;
};
std::unordered_map<void*, MMAnimState> g_animState;

struct MMAnimKeyState {
    float frame = 0.0f;        // free-run playhead
    bool hasFrame = false;
    std::string lastCsab;      // clip this actor was driven with
    float lastFrame = 0.0f;    // and at what frame
    std::string morphOut;      // frozen OUTGOING clip during a morph ("" = none)
    float morphOutFrame = 0.0f;
};
// Per-ACTOR, and RUN-scoped: keyed by a ZeldaArena address the next run reuses, exactly like
// g_animState. Cleared in Zelda3D_MM_ModelResetRunState alongside it.
std::unordered_map<const void*, MMAnimKeyState> g_animKey;


// Both of these are RUN-scoped, and neither says so by its type.
//
// g_animState is keyed by `jointTable`, a ZeldaArena address, and its values hold `animOtr` --
// `skelAnime->animation`, which is memory this run owns. The arena is rebuilt per run, so run 2
// allocates jointTables at the SAME addresses run 1 used: a lookup then HITS on a stale entry and
// hands back run 1's animation pointer, which the caller compares with strcmp. Nothing about that
// failure looks like a stale cache -- it looks like the wrong animation being selected.
//
// g_pending holds a raw MM Actor*, parked between the divert point and the draw.
// ZELDA3D_MM_PHASE_REPORT=1 -- per (model, clip), the RANGE the sampled CSAB frame actually took.
// The live question the offline checks cannot answer is not "which clip" but "did the playhead
// MOVE": a correct clip sampled at a frozen phase renders exactly like a broken decoder. Recorded
// as min/max/samples rather than a per-frame line so the negative is unmistakable and self-
// describing -- a stuck playhead prints "f 5.00..5.00 over 240 samples", which no amount of log
// volume would have made obvious. Dumped once at run-state reset, so it costs nothing per frame
// beyond two comparisons.
struct MMPhaseStat { float lo = 1e30f, hi = -1e30f; long n = 0; float dur = 0.0f; bool locked = false;
                     long morphN = 0; float morphMax = 0.0f;
                     // Distinct ACTORS contributing. Without it this report can manufacture a false
                     // STUCK: stats are keyed by (model,clip) but the playhead is per-actor, so four
                     // Gerudos each drawn once all start at f=0 and aggregate to "n=4, range
                     // 0.00..0.00" -- indistinguishable from one actor frozen for four frames.
                     std::set<const void*> actors; };
std::map<std::pair<int, std::string>, MMPhaseStat> g_phaseStats;
static void mmDumpPhaseStats(void);
static bool mmPhaseReport() {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("ZELDA3D_MM_PHASE_REPORT");
        on = (e && e[0] && e[0] != '0') ? 1 : 0;
        // atexit as well as the run-state reset. The reset runs at RunBEGIN, tidying the PREVIOUS
        // run -- so a single-run session would have reached teardown having printed nothing, and a
        // report that cannot print on the commonest way of running the game is not a report.
        if (on == 1) atexit(mmDumpPhaseStats);
    }
    return on == 1;
}

static void mmDumpPhaseStats(void) {
    if (!mmPhaseReport()) return;
    // Denominator first: "no stuck clips" says nothing without how many were sampled at all.
    fprintf(stderr, "[MM3D-PHASE] %zu (model,clip) pair(s) sampled\n", g_phaseStats.size());
    int stuck = 0;
    for (const auto& kv : g_phaseStats) {
        const MMPhaseStat& st = kv.second;
        // n<2 is NOT "stuck": one sample cannot show movement. Reporting it as stuck is the same
        // class of error this whole report exists to catch -- a verdict its evidence cannot support.
        const bool moved = (st.hi - st.lo) > 1e-3f;
        // Samples per ACTOR is the only figure that can support a stuck verdict. n/actors <= 1 means
        // no single actor was observed twice, so nothing could have advanced by construction.
        const size_t nActors = st.actors.empty() ? 1 : st.actors.size();
        const bool enough = st.n >= 2 && (size_t)st.n >= nActors * 2;
        const char* verdict = !enough ? "THIN" : (moved ? "MOVED" : "STUCK");
        if (!moved && enough) stuck++;
        fprintf(stderr, "[MM3D-PHASE] %-6s model=%d %-28s f %.2f..%.2f dur=%.0f n=%ld %s morph=%ld/%.2f actors=%lu\n",
                verdict, kv.first.first, kv.first.second.c_str(),
                st.lo, st.hi, st.dur, st.n, st.locked ? "phase-locked" : "free-run",
                st.morphN, st.morphMax, (unsigned long)nActors);
    }
    long morphTotal = 0;
    for (const auto& kv : g_phaseStats) morphTotal += kv.second.morphN;
    fprintf(stderr, "[MM3D-PHASE] morph path fired on %ld sample(s) across all pairs.%s\n", morphTotal,
            morphTotal == 0 ? "  Either no transition happened while this was watching, or MM never"
                              " reports a nonzero morphWeight -- do NOT read 0 as 'morph works'." : "");
    fprintf(stderr, "[MM3D-PHASE] %d of %zu pair(s) with >=2 samples PER ACTOR never advanced."
            "  (THIN = too few samples per actor to say either way.)%s\n", stuck, g_phaseStats.size(),
            g_phaseStats.empty() ? "  NOTE: zero pairs sampled -- this run measured NOTHING, which is"
                                   " NOT the same as 'nothing was stuck'." : "");
    g_phaseStats.clear();
    fflush(stderr);
}

extern "C" void Zelda3D_MM_ModelResetRunState(void) {
    const size_t droppedAnimStates = g_animState.size();
    const size_t droppedAnimKeys = g_animKey.size();
    const bool hadPending = g_pending.actor != nullptr;

    g_animState.clear();
    g_animKey.clear();
    g_pending = MMPending{};

    // Printed with the count, pass or fail: "cleared" alone cannot distinguish a first run (nothing
    // to drop) from a run that inherited a full table, and the count IS the evidence that the table
    // was surviving runs at all.
    fprintf(stderr, "MM3D CORE: model run-state reset -- dropped %zu stale anim-state entr%s, %zu "
            "anim-key entr%s, pending actor: %s.\n",
            droppedAnimStates, droppedAnimStates == 1 ? "y" : "ies",
            droppedAnimKeys, droppedAnimKeys == 1 ? "y" : "ies", hadPending ? "yes" : "none");
    mmDumpPhaseStats();
    fflush(stderr);
}

// --- N64 animation OTR -> 3DS CSAB base-name map. Seeded small and grown, exactly as OoT's
// kZelda3dAnimMaps did. An unmapped anim falls back to the model's default idle (below), so a
// skinned actor always stands rather than freezing at bind pose. The [MM3D-ANIM] log (emitted
// once per unmapped OTR) harvests the real N64 OTR strings to fill this table.
struct MMAnimMap { const char* n64otr; const char* csab; };
static const MMAnimMap kMMAnimMaps[] = {
// Generated table: N64 animation OTR -> MM3D CSAB clip, produced by
// tools/gen_mm_animmap.py (regenerate after asset/ROM changes; do not hand-edit the .inc).
// Primary signal is the 2ship asset XMLs' annotations, which record the asset's ORIGINAL (romaji)
// name and are the only thing that crosses the English<->Japanese naming gap. Four dialects are
// read, in this precedence: an explicit MM3D-side name ("MM3D name is X" / 'Named "X" in MM3D'),
// then "Not present in MM3D" as a NEGATIVE that suppresses guessing, then "Original name is X".
// A handful of mappings that no rule generalises to live in the generator's VERIFIED_OVERRIDES
// table with their evidence; `gen_mm_animmap.py --verify-overrides` re-derives them from the ROM.
// Unmapped animations still fall back to the model's default idle CSAB.
#include "mm3d_animmap.inc"
};
static const char* resolveAutoCsab(const char* animOtr) {
    if (animOtr == nullptr) return nullptr;
    // skelAnime->animation is an OTR resource path, usually with a "__OTR__" signature prefix
    // (2s2h keeps ogAnim there). Match against the prefix-stripped key.
    const char* key = animOtr;
    if (strncmp(key, "__OTR__", 7) == 0) key += 7;
    for (const auto& m : kMMAnimMaps) {
        if (strcmp(m.n64otr, key) == 0) return m.csab;
    }
    return nullptr;
}

// GAR basename -> the clip to play when the live N64 animation has no kMMAnimMaps entry.
// Generated, because choosing it needs the N64<->3DS mapping: the "*wait*" substring below cannot
// see a JAPANESE-named idle (zf2_matsu, hs_matsu — matsu is literally "wait"), and for those actors
// the runtime was falling through to "first CSAB in the archive", which is GAR ordering rather than
// evidence. It picked zf2_aruku ("walk") for Dinolfos and pr_damage for object_pr.
struct MMDefaultAnim { const char* gar; const char* csab; };
static const MMDefaultAnim kMMDefaultAnims[] = {
#include "mm3d_defaultanim.inc"
};

// Resolve (once) the model's default idle CSAB: the generated table first, then a "*wait*" clip
// (MM convention: dog_wait, an_wait), then the first CSAB. Data-driven — no per-actor hardcoding.
// The last step is a KNOWN GAP for the ~25 actors the generator could not determine an idle for --
// it is arbitrary, and the generator names them rather than letting the table imply they are solved.
static const char* defaultAnimFor(Loaded* lm) {
    if (lm->defaultAnimResolved) return lm->defaultAnim.empty() ? nullptr : lm->defaultAnim.c_str();
    lm->defaultAnimResolved = true;
    if (!lm->garName.empty()) {
        for (const auto& d : kMMDefaultAnims) {
            if (lm->garName == d.gar) { lm->defaultAnim = d.csab; return lm->defaultAnim.c_str(); }
        }
    }
    if (lm->gar) {
        const Zelda3D::GarFile* first = nullptr;
        for (const auto& f : lm->gar->files()) {
            if (f.type != "csab") continue;
            if (first == nullptr) first = &f;
            if (f.name.find("wait") != std::string::npos) { lm->defaultAnim = f.name; break; }
        }
        if (lm->defaultAnim.empty() && first != nullptr) lm->defaultAnim = first->name;
    }
    return lm->defaultAnim.empty() ? nullptr : lm->defaultAnim.c_str();
}

// Get-or-parse the CSAB named `base` from this model's GAR (MM CSAB files are flat-named, e.g.
// "dog_wait", type "csab"). Mirrors OoT's getCsab, minus the ZAR "Anim/" pathing. Cached per model.
static Zelda3D::Csab* getCsabMM(Loaded* lm, const char* base) {
    if (base == nullptr || base[0] == '\0' || !lm->gar) return nullptr;
    std::string key(base);
    auto it = lm->anims.find(key);
    if (it != lm->anims.end()) return it->second.get();
    const Zelda3D::GarFile* af = nullptr;
    for (const auto& f : lm->gar->files()) {
        if (f.type == "csab" && f.name == key) { af = &f; break; }
    }
    std::unique_ptr<Zelda3D::Csab> csab;
    if (af != nullptr) {
        csab = std::make_unique<Zelda3D::Csab>(lm->gar->read(*af));
        if (!csab->ok()) {
            fprintf(stderr, "[MM3D] Csab %s: %s\n", key.c_str(), csab->error().c_str());
            csab.reset();
        }
    } else {
        fprintf(stderr, "[MM3D] csab not found: %s\n", key.c_str());
    }
    it = lm->anims.emplace(std::move(key), std::move(csab)).first;
    return it->second.get();
}

// Sample CSAB `name` at `frame` and upload the skin + bind matrices. Mirrors OoT's
// Zelda3D_UpdateAnim. name==NULL -> clear (bind pose).
static void mmUpdateAnim(int modelId, const char* name, float frame) {
    Loaded* lm = loadModel(modelId);
    if (lm == nullptr || !lm->ok || !lm->cmb) { Zelda3D_GL_SetBones(modelId, nullptr, 0); return; }
    if (name == nullptr || name[0] == '\0') { Zelda3D_GL_SetBones(modelId, nullptr, 0); return; }
    Zelda3D::Csab* anim = getCsabMM(lm, name);
    if (anim == nullptr) { Zelda3D_GL_SetBones(modelId, nullptr, 0); return; }
    std::vector<std::array<float, 16>> sm;
    anim->skinMatrices(*lm->cmb, frame, sm);
    const auto& bind = lm->cmb->boneMatrices();
    Zelda3D_GL_SetBoneBind(modelId, bind.empty() ? nullptr : bind.front().data(), (int)bind.size());
    Zelda3D_GL_SetBones(modelId, sm.empty() ? nullptr : sm.front().data(), (int)sm.size());
}

// MORPH variant of mmUpdateAnim: cross-fade the INCOMING clip (inName@fIn) toward the frozen
// OUTGOING clip (outName@fOut) by `weight` (= the live N64 skelAnime->morphWeight, 1 -> 0 across the
// transition). Ported from OoT's Zelda3D_UpdateAnimMorph and using the SAME shared primitive
// (Csab::skinMatricesMorph) -- MM's SkelAnime morph model is the same one, so this is a port rather
// than a second implementation. If the outgoing CSAB cannot be resolved we sample the incoming clip
// plainly instead of dropping the pose: a hard cut is worse than no morph, but both beat a T-pose.
static void mmUpdateAnimMorph(int modelId, const char* inName, float fIn, const char* outName,
                              float fOut, float weight) {
    Loaded* lm = loadModel(modelId);
    if (lm == nullptr || !lm->ok || !lm->cmb) { Zelda3D_GL_SetBones(modelId, nullptr, 0); return; }
    if (inName == nullptr || inName[0] == '\0') { Zelda3D_GL_SetBones(modelId, nullptr, 0); return; }
    Zelda3D::Csab* in = getCsabMM(lm, inName);
    if (in == nullptr) { Zelda3D_GL_SetBones(modelId, nullptr, 0); return; }
    Zelda3D::Csab* out = (outName != nullptr && outName[0] != '\0') ? getCsabMM(lm, outName) : nullptr;
    std::vector<std::array<float, 16>> sm;
    if (out != nullptr) in->skinMatricesMorph(*lm->cmb, fIn, *out, fOut, weight, sm);
    else in->skinMatrices(*lm->cmb, fIn, sm);
    const auto& bind = lm->cmb->boneMatrices();
    Zelda3D_GL_SetBoneBind(modelId, bind.empty() ? nullptr : bind.front().data(), (int)bind.size());
    Zelda3D_GL_SetBones(modelId, sm.empty() ? nullptr : sm.front().data(), (int)sm.size());
}

// Drive the model's CSAB `name`, phase-locked to the live N64 anim: play the 3DS clip at the SAME
// fractional progress as the N64 animation (csab_frame = (n64Cur/n64Len) * csab_duration). When the
// N64 length is unusable (short idle stub / not yet known) free-run at `rate` frames/draw so the
// clip still animates. Mirrors OoT's Zelda3D_UpdateAnimAuto core (minus the morph polish, added later).
static void mmUpdateAnimAuto(int modelId, const void* key, const char* name, float rate,
                             float n64Cur, float n64Len, float morphWeight) {
    // Keyed by the ACTOR (its jointTable), not by modelId. MM shares one model across many actors --
    // model 18 drew 26451 kamome_fly samples in one tour, i.e. a whole flock of seagulls -- so
    // per-model bookkeeping thrashes between instances: actor B draws with a different clip, that
    // reads as a "transition" for actor A, and A then cross-fades toward B's pose. The playhead has
    // the same problem: one free-run counter shared by every instance of a model. jointTable is the
    // identity g_animState already uses, so morph state and anim state now agree on what an actor is.
    if (name == nullptr || name[0] == '\0') {
        g_animKey.erase(key);
        mmUpdateAnim(modelId, nullptr, 0);
        return;
    }
    MMAnimKeyState& ks = g_animKey[key];
    Loaded* lm = loadModel(modelId);
    float dur = 0.0f;
    if (lm != nullptr && lm->ok && lm->cmb) {
        Zelda3D::Csab* c = getCsabMM(lm, name);
        if (c != nullptr) dur = (float)c->duration();
    }
    float f;
    if (n64Len > 4.0f && n64Cur >= 0.0f && dur > 0.0f) {
        f = (n64Cur / n64Len) * dur; // phase-lock
        ks.frame = f; ks.hasFrame = true;
    } else {
        // Free-run, WRAPPED. Left unwrapped this is a counter that only ever grows: measured live,
        // pst_model reached f=3198 on a 31-frame clip in under two minutes. Csab::animFrame wraps
        // with `while (frame > last) frame -= last`, so the cost of every sample grows linearly with
        // how long the process has been running -- ~103 iterations at 3198, and unbounded after an
        // hour of play. Wrapping here keeps the playhead in range and leaves that loop a no-op.
        f = ks.hasFrame ? ks.frame + rate : 0.0f;
        if (dur > 0.0f && f > dur) f = std::fmod(f, dur);
        ks.frame = f; ks.hasFrame = true;
    }
    if (mmPhaseReport()) {
        MMPhaseStat& st = g_phaseStats[{ modelId, std::string(name) }];
        st.lo = std::min(st.lo, f);
        st.hi = std::max(st.hi, f);
        st.dur = dur;
        st.locked = (n64Len > 4.0f && n64Cur >= 0.0f && dur > 0.0f);
        st.n++;
        // Whether the MORPH path actually fires. New code that never runs is worse than no code:
        // it looks like a feature. If MM's SkelAnime never reports a nonzero morphWeight, every
        // pair below reads morph=0 and the port is inert -- which is a finding, not a pass.
        if (morphWeight > 0.0f) { st.morphN++; st.morphMax = std::max(st.morphMax, morphWeight); }
        st.actors.insert(key);
    }
    // MORPH bookkeeping, ported from OoT's Zelda3D_UpdateAnimAuto. On a real transition (the
    // resolved clip changed while the N64 side is mid-morph) freeze the clip we are LEAVING at its
    // last frame, then cross-fade the incoming clip toward that frozen pose while morphWeight > 0.
    // Without it every transition hard-cuts, which reads as a one-frame limb pop.
    const bool hadLast = !ks.lastCsab.empty();
    const bool csabChanged = !hadLast || ks.lastCsab != name;
    if (csabChanged) {
        if (morphWeight > 0.0f && hadLast) {
            ks.morphOut = ks.lastCsab;       // the clip THIS actor is leaving
            ks.morphOutFrame = ks.lastFrame;
        } else {
            ks.morphOut.clear();             // hard cut / first anim
        }
    }
    if (morphWeight <= 0.0f) ks.morphOut.clear();
    ks.lastCsab = name;
    ks.lastFrame = f;

    if (!ks.morphOut.empty() && morphWeight > 0.0f) {
        mmUpdateAnimMorph(modelId, name, f, ks.morphOut.c_str(), ks.morphOutFrame, morphWeight);
    } else {
        mmUpdateAnim(modelId, name, f);
    }
}

// Forward-declared C bridge implemented in mm3d_draw.c (has access to POLY_OPA_DISP
// / Gfx_SetupDL25_Opa / MATRIX_FINALIZE_AND_LOAD via the decomp global.h).
extern "C" void Zelda3D_MM_EmitModelDraw(void* play, void* actor, int modelId, float worldScale,
                                          float groundOffset);

void Zelda3D_MM_SetPending(void* actor, int modelId, float worldScale, float groundOffset) {
    g_pending.actor = actor;
    g_pending.modelId = modelId;
    g_pending.scale = worldScale;
    g_pending.groundOff = groundOffset;
}

void Zelda3D_MM_CaptureAnimState(void* jointTable, void* animation, float curFrame, float animLength,
                                 float morphWeight) {
    if (jointTable == nullptr) return;
    MMAnimState& s = g_animState[jointTable];
    s.animOtr = static_cast<const char*>(animation);
    s.curFrame = curFrame;
    s.animLength = animLength;
    s.morphWeight = morphWeight;
}

int Zelda3D_MM_SkelAnimeDrawRaw(struct PlayState* play, void** skeleton, void* jointTableV, int limbCount) {
    if (g_pending.modelId < 0 || g_pending.actor == nullptr) return 0;
    if (skeleton == nullptr || jointTableV == nullptr || limbCount <= 0) return 0;

    // Recover this actor's live N64 anim state (captured in SkelAnime_Update, keyed by jointTable).
    const char* animOtr = nullptr;
    float n64Cur = -1.0f, n64Len = 0.0f, n64Morph = 0.0f;
    auto asIt = g_animState.find(jointTableV);
    if (asIt != g_animState.end()) {
        animOtr = asIt->second.animOtr;
        n64Cur = asIt->second.curFrame;
        n64Len = asIt->second.animLength;
        n64Morph = asIt->second.morphWeight; // captured since the start; only now consumed
    }

    // Select the 3DS CSAB from the live N64 animation; unmapped -> the model's default idle so the
    // actor stands rather than freezing. Harvest unmapped OTRs (once each) to grow kMMAnimMaps.
    const char* csab = resolveAutoCsab(animOtr);
    if (csab == nullptr) {
        Loaded* lm = loadModel(g_pending.modelId);
        if (lm != nullptr) csab = defaultAnimFor(lm);
        if (animOtr != nullptr) {
            static std::set<std::string> seen;
            if (seen.insert(animOtr).second) {
                fprintf(stderr, "[MM3D-ANIM] model=%d unmapped n64='%s' -> default '%s'\n",
                        g_pending.modelId, animOtr, csab ? csab : "(none)");
            }
        }
    }

    mmUpdateAnimAuto(g_pending.modelId, jointTableV, csab, /*rate=*/1.0f, n64Cur, n64Len, n64Morph);
    // ZELDA3D_MM_DBG_SKIN=1 — one line per skinned emit: which actor, where it actually is, and
    // which model got drawn there. Added to chase a skinned model rendering on top of Link at the
    // South Clock Town spawn; the answer is the emit whose actor position does NOT match where the
    // model visibly appears. Gated because it is per-actor per-frame.
    {
        static int dbg = -1;
        if (dbg < 0) {
            const char* e = getenv("ZELDA3D_MM_DBG_SKIN");
            dbg = (e != nullptr && e[0] != '\0' && e[0] != '0') ? 1 : 0;
        }
        if (dbg) {
            Actor* a = static_cast<Actor*>(g_pending.actor);
            fprintf(stderr, "[MM3D-SKIN] model=%d actorId=0x%03X obj=? pos=(%.0f,%.0f,%.0f) "
                            "scale=%.3f groundOff=%.1f csab=%s\n",
                    g_pending.modelId, a ? (unsigned)a->id : 0u,
                    a ? a->world.pos.x : 0.0f, a ? a->world.pos.y : 0.0f, a ? a->world.pos.z : 0.0f,
                    g_pending.scale, g_pending.groundOff, csab ? csab : "(none)");
        }
    }
    Zelda3D_MM_EmitModelDraw(play, g_pending.actor, g_pending.modelId, g_pending.scale,
                             g_pending.groundOff);
    // Drawn — clear so a second SkelAnime call inside this same actor draw doesn't re-emit.
    g_pending.modelId = -1;
    g_pending.actor = nullptr;
    return 1;
}

void Zelda3D_MM_AfterActorDraw(void) {
    g_pending.modelId = -1;
    g_pending.actor = nullptr;
}

// STAGE 3 auto-scale + ground-offset. See mm3d_model.h for the derivation.
float Zelda3D_MM_ModelBoneLenSum(int modelId) {
    Loaded* lm = loadModel(modelId);
    if (lm == nullptr || !lm->ok || !lm->cmb) return 0.0f;
    float sum = 0.0f;
    for (const auto& bn : lm->cmb->bones()) {
        if (bn.parent < 0) continue; // root translation is placement, not a bone length
        sum += std::sqrt(bn.trans[0] * bn.trans[0] + bn.trans[1] * bn.trans[1] +
                         bn.trans[2] * bn.trans[2]);
    }
    return sum;
}

// Debug counterpart to the [MM3D-BONE-N64] dump: print the first `n` CMB bone translations so the
// N64 skeleton and the 3DS rig can be compared ELEMENT-WISE, not just by their sums.
void Zelda3D_MM_DumpModelBones(int modelId, int n) {
    Loaded* lm = loadModel(modelId);
    if (lm == nullptr || !lm->ok || !lm->cmb) return;
    int i = 0;
    for (const auto& bn : lm->cmb->bones()) {
        if (i >= n) break;
        if (bn.parent >= 0) {
            const float len = std::sqrt(bn.trans[0] * bn.trans[0] + bn.trans[1] * bn.trans[1] +
                                        bn.trans[2] * bn.trans[2]);
            fprintf(stderr, "[MM3D-BONE-CMB] model=%d bone=%d trans=(%.1f,%.1f,%.1f) |v|=%.2f\n",
                    modelId, i, bn.trans[0], bn.trans[1], bn.trans[2], len);
        }
        i++;
    }
}

float Zelda3D_MM_ModelMinY(int modelId) {
    Loaded* lm = loadModel(modelId);
    if (lm == nullptr || !lm->ok) return 0.0f;
    float mn = 1e30f;
    for (const auto& g : lm->groups)
        for (const auto& v : g.verts) if (v.pos[1] < mn) mn = v.pos[1];
    return (mn < 1e29f) ? mn : 0.0f;
}

void Zelda3D_MM_OverridePending(float worldScale, float groundOffset) {
    if (g_pending.modelId < 0) return;
    g_pending.scale = worldScale;
    // STOPGAP: two MM3D archives (sdn, cs) yield a minY of ~1e34+ CMB units — the
    // BindPose vert data blends through weird bone matrices during buildDrawGroups
    // and accumulates junk on those specific rigs. Proper fix: audit cmb.cpp's
    // smooth-skinning blend for those files. Until then, cap the ground lift so a
    // 1e38 Matrix_Translate doesn't poison the pose stack for every other actor.
    // Humanoid CMB feet sit no lower than a few thousand local units, so the cap
    // is loose enough to preserve every well-formed rig.
    if (!(groundOffset > -1e5f && groundOffset < 1e5f)) groundOffset = 0.0f;
    g_pending.groundOff = groundOffset;
    // One log per model — verify Stage 3 auto-scale is landing on live rigs.
    // Gated by ZELDA3D_MM_SCALE_LOG=1 to keep the default log clean.
    static int sLog = -1;
    if (sLog < 0) {
        const char* v = getenv("ZELDA3D_MM_SCALE_LOG");
        sLog = (v != nullptr && v[0] != '\0' && v[0] != '0') ? 1 : 0;
    }
    if (sLog) {
        static std::unordered_map<int, int> seen;
        if (seen.find(g_pending.modelId) == seen.end()) {
            seen[g_pending.modelId] = 1;
            fprintf(stderr, "[MM3D-SCALE] modelId=%d worldScale=%.5f groundOff=%.3f\n",
                    g_pending.modelId, worldScale, groundOffset);
        }
    }
}

int Zelda3D_MM_PendingModelId(void) { return g_pending.modelId; }

void Zelda3D_ListModels(void (*emitLine)(const char* line, void* user), void* user) {
    if (emitLine == nullptr) return;
    // Sort by objectId for stable output.
    std::vector<int> objs;
    objs.reserve(g_objectToModel.size());
    for (const auto& kv : g_objectToModel) {
        if (kv.second >= 0) objs.push_back(kv.first);
    }
    std::sort(objs.begin(), objs.end());
    char line[192];
    for (int obj : objs) {
        int mid = g_objectToModel[obj];
        const char* nm = "?";
        auto n = g_objectName.find(obj);
        if (n != g_objectName.end()) nm = n->second.c_str();
        snprintf(line, sizeof(line), "  obj=0x%03X (%s) -> modelId=%d scale=%.4f", obj, nm, mid,
                 g_models[mid].worldScale);
        emitLine(line, user);
    }
    if (objs.empty()) {
        emitLine("  (no auto-mapped MM3D models yet)", user);
    }
}

} // extern "C"

// Shared ROM handle for sibling zelda3d modules (see mm3d_model.h). Keeps ONE CtrRom open.
Zelda3D::CtrRom* Zelda3D_MM_Rom() {
    return rom();
}
