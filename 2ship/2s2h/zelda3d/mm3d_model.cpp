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
#include "asset/mat4.h" // Mat4 helpers for the SkelAnime retarget port

#include <array>
#include <cmath>
#include <functional>

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
    bool skinned = false; // true = draw via SkelAnime intercept (retarget from N64 pose)
};
std::vector<ModelSpec> g_models;
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
        // Skinned — the Stage-2 SkelAnime intercept IS wired (z_skelanime.c choke points ->
        // Zelda3D_MM_InterceptSkelAnime -> SkelAnimeDrawRaw -> mmUpdateAnimN64, which poses the
        // CMB from the LIVE N64 jointTable via the identity bone->limb retarget). Accepting a
        // skinned archive here therefore yields a live-animated MM3D draw, NOT a T-pose.
        // Graded POSITIVE on complex rigs in default South Clock Town (dog 12-bone, humanoid
        // NPC 15-20 bone pose correctly — docs/MM_SKELANIME_PORT.md Stage-2 status).
        // Still behind the ZELDA3D_MM_SKINNED_TPOSE env gate (legacy name — no longer T-pose)
        // pending broader game-wide validation + per-archive bone-maps for rigs whose CMB bone
        // order diverges from the N64 limb order (mmUpdateAnimN64's identity assumption).
        static int sSkinnedEnable = -1;
        if (sSkinnedEnable < 0) {
            const char* v = getenv("ZELDA3D_MM_SKINNED_TPOSE");
            sSkinnedEnable = (v != nullptr && v[0] != '\0' && v[0] != '0') ? 1 : 0;
        }
        if (!sSkinnedEnable) {
            fprintf(stderr, "[MM3D] skip obj=0x%03X (%s): skinned (%zu bones)\n",
                    objectId, name, probeCmb.bones().size());
            g_objectToModel[objectId] = -1;
            return -1;
        }
        fprintf(stderr, "[MM3D] skinned obj=0x%03X (%s): %zu bones (Stage-2 live-posed draw)\n",
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

// STAGE 2 SkelAnime port — live N64-anim retarget for skinned MM3D actors.
// See docs/MM_SKELANIME_PORT.md. Mirrors OoT's Zelda3D_UpdateAnimN64Mapped
// (Shipwright/soh/src/zelda3d/anim/zelda3d_anim.cpp) — identity bone->limb map,
// N64 rotation REPLACES the CMB rest rot in Rz*Ry*Rx order.

// Pending state — set by mm3d_draw's TryDrawActor when a skinned model is
// resolved; consumed by SkelAnimeDrawRaw and cleared in AfterActorDraw.
struct MMPending {
    void* actor = nullptr; // MM Actor*
    int modelId = -1;
    float scale = 1.0f;
    float groundOff = 0.0f;
};
MMPending g_pending;

// Retarget the CMB bones from the live N64 jointTable (identity boneId <-> limbIdx
// mapping). Uploads skin matrices + bind matrices to the renderer.
static void mmUpdateAnimN64(int modelId, const int16_t* jointRots, int rotCount) {
    using namespace Zelda3D;
    Loaded* lm = loadModel(modelId);
    if (lm == nullptr || !lm->ok || !lm->cmb) {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }
    const auto& bones = lm->cmb->bones();
    const auto& bind = lm->cmb->boneMatrices();
    if (bind.empty()) {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }
    const float kBinangToRad = 3.14159265358979f / 32768.0f;

    std::vector<Mat4> aw(bind.size(), matId());
    std::vector<char> done(bind.size(), 0);
    std::vector<const CmbBone*> byId(bind.size(), nullptr);
    for (const auto& bn : bones) {
        if (bn.id >= 0 && (size_t)bn.id < byId.size()) byId[bn.id] = &bn;
    }

    std::function<Mat4(int)> world = [&](int id) -> Mat4 {
        if (id < 0 || (size_t)id >= aw.size() || byId[id] == nullptr) return matId();
        if (done[id]) return aw[id];
        const CmbBone* bn = byId[id];
        Mat4 L = matT(bn->trans[0], bn->trans[1], bn->trans[2]);
        // Identity retarget: bone id == limb index. Stage 3 will replace this with
        // a per-archive bone-map lookup for rigs whose topology diverges.
        int limb = id;
        if (limb >= 0 && limb < rotCount) {
            float rx = jointRots[limb * 3 + 0] * kBinangToRad;
            float ry = jointRots[limb * 3 + 1] * kBinangToRad;
            float rz = jointRots[limb * 3 + 2] * kBinangToRad;
            L = matMul(L, matMul(matMul(matRz(rz), matRy(ry)), matRx(rx)));
        } else {
            L = matMul(L, matMul(matMul(matRz(bn->rot[2]), matRy(bn->rot[1])), matRx(bn->rot[0])));
        }
        L = matMul(L, matS(bn->scale[0], bn->scale[1], bn->scale[2]));
        Mat4 W = (bn->parent < 0) ? L : matMul(world(bn->parent), L);
        aw[id] = W;
        done[id] = 1;
        return W;
    };
    for (const auto& bn : bones) world(bn.id);

    std::vector<std::array<float, 16>> sm(bind.size());
    for (size_t id = 0; id < bind.size(); id++) sm[id] = matMul(aw[id], matInverse(bind[id]));
    Zelda3D_GL_SetBoneBind(modelId, bind.front().data(), (int)bind.size());
    Zelda3D_GL_SetBones(modelId, sm.front().data(), (int)sm.size());
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

int Zelda3D_MM_SkelAnimeDrawRaw(struct PlayState* play, void** skeleton, void* jointTableV, int limbCount) {
    if (g_pending.modelId < 0 || g_pending.actor == nullptr) return 0;
    if (skeleton == nullptr || jointTableV == nullptr || limbCount <= 0) return 0;
    // Vec3s* jointTable — index 0 is the root position; joint rotations start at index 1.
    // Each joint = 3 s16 -> pass as (const int16_t*) starting 3 s16 past the base.
    const int16_t* base = static_cast<const int16_t*>(jointTableV);
    const int16_t* jointRots = base + 3; // skip jointTable[0]
    mmUpdateAnimN64(g_pending.modelId, jointRots, limbCount);
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
