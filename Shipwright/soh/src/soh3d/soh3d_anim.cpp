// SoH3D animation / pose layer — resolve a model's OoT3D CSAB to per-bone skin matrices each frame,
// the N64-jointTable retarget path, posed-feet grounding, the pose-discontinuity scanner, and the
// resolved-pose capture (skindump). Split out of soh3d_model.cpp (pure move). Uses the model core
// (LoadedModel / loadModel) via soh3d_model_internal.h.
#include "soh3d.h"
#include "soh3d_model_internal.h"
#include "asset/cmb.h"
#include "asset/csab.h"
#include "asset/mat4.h"
#include "fast/soh3d_gl.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


// Matches the struct in soh3d.h (C ABI). Per OoT3D bone: which N64 jointRots index drives it
// and HOW. mode 0=rest (keep CMB rest rot), 1=replace (local rot := N64 rot), 2=left (C·R_n64),
// 3=right (R_n64·C). C is a row-major 3x3 constant rest-frame correction for bones whose OoT3D
// rest frame diverges from the N64 limb's (Grezzo re-rigged Link's spine/arms). See
// tools/soh3d_link_retarget_derive.py and [[soh3d-n64anim-retarget]].
typedef struct {
    signed char limb;
    unsigned char mode; // 0 rest, 1 replace, 2 left C·R, 3 right R·C, 4 two-sided C·R·C2
    float C[9];
    float C2[9];        // right factor for mode 4 (identity otherwise)
} SoH3dBoneCorr;

// Get-or-load the parsed CSAB for `animName` (base name or full "Anim/<n>.csab"),
// caching it on the model. Returns nullptr if missing/unparseable (logged once via
// the cached null entry). Shared by the frame- and phase-based update entry points.
static SoH3D::Csab* getCsab(LoadedModel* lm, const char* animName) {
    std::string nm(animName);
    // Accept three forms: a bare base ("ge1_s_wait" -> "Anim/ge1_s_wait.csab", the common case),
    // an explicit "Anim/..." path, or any verbatim zar-relative .csab path. The link rig stores its
    // CSABs under "boy/anim/" / "child/anim/" (not "Anim/") AND splits them across those two dirs by
    // age, so for a bare base we also do a basename scan: match any file ending "/<base>.csab". Each
    // zar holds exactly one file per basename, so this resolves the age dir automatically.
    bool verbatim = nm.rfind("Anim/", 0) == 0 || (nm.size() > 5 && nm.compare(nm.size() - 5, 5, ".csab") == 0);
    std::string full = verbatim ? nm : ("Anim/" + nm + ".csab");
    auto it = lm->anims.find(full);
    if (it == lm->anims.end()) {
        const SoH3D::ZarFile* af = nullptr;
        for (const auto& f : lm->zar->files()) if (f.name == full) { af = &f; break; }
        if (!af && !verbatim) { // basename fallback: "<base>.csab" anywhere in the zar (link boy/child/anim)
            // Try the verbatim base first, then the child "cl_" prefix: Grezzo prefixes some child-age
            // link anims with cl_ (e.g. boy dm_Tbox_open <-> child cl_dm_Tbox_open), so a single
            // basename map entry resolves for either age. (See tools/gen_player_animmap.py resolves_in.)
            const std::string suffixes[2] = { "/" + nm + ".csab", "/cl_" + nm + ".csab" };
            for (const std::string& suffix : suffixes) {
                for (const auto& f : lm->zar->files()) {
                    if (f.name.size() >= suffix.size() && f.name.compare(f.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                        af = &f; full = f.name; // cache under the real path so a re-resolve hits directly
                        break;
                    }
                }
                if (af) break;
            }
            auto it2 = lm->anims.find(full); // the resolved path may already be cached
            if (it2 != lm->anims.end()) return it2->second.get();
        }
        std::unique_ptr<SoH3D::Csab> csab;
        if (af) {
            csab = std::make_unique<SoH3D::Csab>(lm->zar->read(*af));
            if (!csab->ok()) { fprintf(stderr, "[SoH3D] Csab %s: %s\n", full.c_str(), csab->error().c_str()); csab.reset(); }
        } else {
            fprintf(stderr, "[SoH3D] anim not found: %s\n", full.c_str());
        }
        it = lm->anims.emplace(full, std::move(csab)).first;
    }
    return it->second.get();
}

// Retarget a live N64 SkelAnime pose onto the OoT3D skeleton. `jointRots` points to the
// actor's per-limb rotations (jointTable[1..limbCount], each a Vec3s of binang x,y,z; the
// caller skips jointTable[0] which is the root translation). OoT3D bone id i corresponds to
// N64 limb (i+1) for same-rig characters (Grezzo preserved the skeletons), so bone i takes
// jointRots[i]. The N64 jointTable already encodes each limb's FULL local orientation (the
// standing pose's big rotations included -- e.g. En_Ge1 limb1 = (-90,0,-90), matching OoT3D
// bone0's rest), exactly like a CSAB rotation track REPLACES the bone's rest rotation. So we
// use the N64 rotation as the local rotation directly (Rz*Ry*Rx, same order as csab.cpp) and
// do NOT compose it with the CMB rest rotation -- composing double-applies the orientation and
// contorts the pose. Convention derived QUANTITATIVELY (tools/soh3d_anim_derive.py: diff CSAB
// ge1_s_wait skin matrices vs N64-joint-driven ones -> struct=replace, euler order ZYX wins
// over every compose variant). L = T(rest)*Rz*Ry*Rx(n64)*S(rest); skin = animWorld*bindInverse.
extern "C" void SoH3D_UpdateAnimN64Mapped(int modelId, const int16_t* jointRots, int rotCount,
                                          const signed char* boneToLimb, int mapCount);

// Posed-feet grounding (#29b): cache this frame's skin matrices for a tracked model. Defined near
// SoH3D_UpdateAnim (inside the same extern "C" region); forward-declared here so the N64-retarget
// update paths below can call it too.
extern "C" {
static void cacheSkinForGround(int modelId, const std::vector<std::array<float, 16>>& sm);
}

extern "C" void SoH3D_UpdateAnimN64(int modelId, const int16_t* jointRots, int rotCount) {
    SoH3D_UpdateAnimN64Mapped(modelId, jointRots, rotCount, nullptr, 0);
}

// As SoH3D_UpdateAnimN64, but with an explicit OoT3D-bone -> N64-limb correspondence
// (`boneToLimb`, indexed by bone id; -1 = no live joint -> keep rest). NULL map = identity
// (bone i <- limb i), the same-rig assumption. The map is the precomputed correspondence
// (tools/soh3d_skel_match.py -> soh3d_bonemap.inc), needed for rigs whose topology differs
// from the N64 skeleton (OoT3D inserts root/reorient bones). See PROGRESS "replace ALL chars".
extern "C" void SoH3D_UpdateAnimN64Mapped(int modelId, const int16_t* jointRots, int rotCount,
                                          const signed char* boneToLimb, int mapCount) {
    using namespace SoH3D;
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb) { SoH3D_GL_SetBones(modelId, nullptr, 0); return; }
    const auto& bones = lm->cmb->bones();
    const auto& bind = lm->cmb->boneMatrices();
    const float kBinangToRad = 3.14159265358979f / 32768.0f;

    std::vector<Mat4> aw(bind.size(), matId());
    std::vector<char> done(bind.size(), 0);
    std::vector<const CmbBone*> byId(bind.size(), nullptr);
    for (const auto& bn : bones)
        if (bn.id >= 0 && (size_t)bn.id < byId.size()) byId[bn.id] = &bn;

    std::function<Mat4(int)> world = [&](int id) -> Mat4 {
        if (id < 0 || (size_t)id >= aw.size() || !byId[id]) return matId();
        if (done[id]) return aw[id];
        const CmbBone* bn = byId[id];
        Mat4 L = matT(bn->trans[0], bn->trans[1], bn->trans[2]);
        // limb = the N64 limb whose rotation drives this OoT3D bone: the precomputed map if
        // present, else identity (bone id == limb index).
        int limb = boneToLimb ? (id < mapCount ? (int)boneToLimb[id] : -1) : id;
        if (limb >= 0 && limb < rotCount) {
            // Use the N64 joint rotation AS the bone's local rotation (replacing the CMB rest
            // rotation), in csab.cpp's Rz*Ry*Rx order. The jointTable already carries the full
            // limb orientation, so composing it with the rest rotation double-applies and
            // contorts (verified by tools/soh3d_anim_derive.py: replace beats compose).
            float rx = jointRots[limb * 3 + 0] * kBinangToRad;
            float ry = jointRots[limb * 3 + 1] * kBinangToRad;
            float rz = jointRots[limb * 3 + 2] * kBinangToRad;
            L = matMul(L, matMul(matMul(matRz(rz), matRy(ry)), matRx(rx)));
        } else {
            // No live joint for this bone: keep its CMB rest orientation (bind pose).
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
    cacheSkinForGround(modelId, sm); // posed-feet grounding for the player path (#29b)
    // Upload bind for correct rigid pose interpolation (see SoH3D_UpdateAnim / interpSkinPose).
    SoH3D_GL_SetBoneBind(modelId, bind.empty() ? nullptr : bind.front().data(), (int)bind.size());
    SoH3D_GL_SetBones(modelId, sm.empty() ? nullptr : sm.front().data(), (int)sm.size());
}

// As SoH3D_UpdateAnimN64Mapped, but each OoT3D bone carries a per-bone CORRECTION (SoH3dBoneCorr,
// indexed by bone id): mode 1 = pure "replace" (local rot := N64 rot, the same-rest case that works
// for Link's legs/head); mode 2/3 = apply a constant rest-frame correction C on the left (C·R_n64)
// or right (R_n64·C) for bones whose OoT3D rest diverges from the N64 limb's (Grezzo re-rigged
// Link's spine/upper arms — see tools/soh3d_link_retarget_derive.py). mode 0 / limb<0 = keep the
// CMB rest pose. Same FK + skin-matrix tail as the Mapped variant.
extern "C" void SoH3D_UpdateAnimN64Corr(int modelId, const int16_t* jointRots, int rotCount,
                                        const SoH3dBoneCorr* corr, int corrCount) {
    using namespace SoH3D;
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb) { SoH3D_GL_SetBones(modelId, nullptr, 0); return; }
    const auto& bones = lm->cmb->bones();
    const auto& bind = lm->cmb->boneMatrices();
    const float kBinangToRad = 3.14159265358979f / 32768.0f;

    auto corrMat = [](const float* c) -> Mat4 {
        Mat4 m = matId();
        m[0] = c[0]; m[1] = c[1]; m[2] = c[2];
        m[4] = c[3]; m[5] = c[4]; m[6] = c[5];
        m[8] = c[6]; m[9] = c[7]; m[10] = c[8];
        return m;
    };

    std::vector<Mat4> aw(bind.size(), matId());
    std::vector<char> done(bind.size(), 0);
    std::vector<const CmbBone*> byId(bind.size(), nullptr);
    for (const auto& bn : bones)
        if (bn.id >= 0 && (size_t)bn.id < byId.size()) byId[bn.id] = &bn;

    std::function<Mat4(int)> world = [&](int id) -> Mat4 {
        if (id < 0 || (size_t)id >= aw.size() || !byId[id]) return matId();
        if (done[id]) return aw[id];
        const CmbBone* bn = byId[id];
        Mat4 L = matT(bn->trans[0], bn->trans[1], bn->trans[2]);
        const SoH3dBoneCorr* c = (corr && id < corrCount) ? &corr[id] : nullptr;
        int limb = c ? c->limb : -1;
        int mode = c ? c->mode : 0;
        if (mode >= 1 && limb >= 0 && limb < rotCount) {
            float rx = jointRots[limb * 3 + 0] * kBinangToRad;
            float ry = jointRots[limb * 3 + 1] * kBinangToRad;
            float rz = jointRots[limb * 3 + 2] * kBinangToRad;
            Mat4 R = matMul(matMul(matRz(rz), matRy(ry)), matRx(rx)); // N64 local rotation (Rz·Ry·Rx)
            if (mode == 2) R = matMul(corrMat(c->C), R);              // left:  C·R_n64
            else if (mode == 3) R = matMul(R, corrMat(c->C));         // right: R_n64·C
            else if (mode == 4) R = matMul(matMul(corrMat(c->C), R), corrMat(c->C2)); // C·R·C2
            else if (mode == 5) {
                // Conjugation C·R·C⁻¹ (C⁻¹ = Cᵀ for a rotation): a change of basis for the rest-frame
                // discrepancy. Unlike mode 2/3 (a one-sided constant that's only right near the tuned
                // pose), this transforms R itself, so a single hand-tuned C holds across the FULL pose
                // range — idle, walk AND the arms-overhead carry pose (#6). Tune just C.
                Mat4 C = corrMat(c->C);
                Mat4 Ci = matId();
                Ci[0] = C[0]; Ci[1] = C[4]; Ci[2] = C[8];
                Ci[4] = C[1]; Ci[5] = C[5]; Ci[6] = C[9];
                Ci[8] = C[2]; Ci[9] = C[6]; Ci[10] = C[10];
                R = matMul(matMul(C, R), Ci);
            }
            L = matMul(L, R);
        } else {
            // No live joint / rest mode: keep the CMB rest orientation (bind pose).
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
    cacheSkinForGround(modelId, sm); // posed-feet grounding for the player path (#29b)
    // Upload bind for correct rigid pose interpolation (see SoH3D_UpdateAnim / interpSkinPose).
    SoH3D_GL_SetBoneBind(modelId, bind.empty() ? nullptr : bind.front().data(), (int)bind.size());
    SoH3D_GL_SetBones(modelId, sm.empty() ? nullptr : sm.front().data(), (int)sm.size());
}

extern "C" {

// Set the model's GPU skinning pose to `animName` (CSAB base name, e.g. "ge1_s_wait")
// at `frame`. animName==NULL/"" resets to the bind pose. Loads the model + caches the
// parsed CSAB on first use; recomputes skin matrices each call (cheap: <=32 bones).
// Call once per game frame before the SoH3D draw. Safe to call repeatedly.
// Per-model procedural per-bone local-rotation deltas (radians, 3 per bone id), set by the auto
// retarget path from an N64 OverrideLimbDraw probe (e.g. the cucco wing-flap) and consumed by the
// next SoH3D_UpdateAnim for that model. Empty = no delta (the common case; static-pose unchanged).
static std::unordered_map<int, std::vector<float>>& boneRotDeltas() {
    static std::unordered_map<int, std::vector<float>> m;
    return m;
}
extern "C" void SoH3D_ClearBoneRotDeltas(int modelId) { boneRotDeltas().erase(modelId); }
extern "C" void SoH3D_SetBoneRotDelta(int modelId, int boneId, float rx, float ry, float rz) {
    if (boneId < 0) return;
    LoadedModel* lm = loadModel(modelId);
    int n = (lm && lm->ok && lm->cmb) ? (int)lm->cmb->boneMatrices().size() : 0;
    if (boneId >= n) return;
    auto& v = boneRotDeltas()[modelId];
    if ((int)v.size() != n * 3) v.assign(n * 3, 0.0f);
    v[boneId * 3 + 0] = rx;
    v[boneId * 3 + 1] = ry;
    v[boneId * 3 + 2] = rz;
}

// Per-model per-bone POST-rotation matrix (row-major 3x3, 9 floats/bone) post-multiplied onto the
// bone's animated local rotation by the CSAB skinner — the OoT3D actor OverrideLimbDraw MTXMODE_APPLY
// channel (En_Ko/En_Sa head/torso tracking). Distinct from boneRotDeltas (euler pre-add, cucco flap):
// a post-multiply in the bone's local frame matches OoT3D's matrix-apply and propagates to children.
static std::unordered_map<int, std::vector<float>>& bonePostRots() {
    static std::unordered_map<int, std::vector<float>> m;
    return m;
}
extern "C" void SoH3D_ClearBonePostRots(int modelId) { bonePostRots().erase(modelId); }
extern "C" void SoH3D_SetBonePostRot(int modelId, int boneId, const float* mat9) {
    if (boneId < 0 || !mat9) return;
    LoadedModel* lm = loadModel(modelId);
    int n = (lm && lm->ok && lm->cmb) ? (int)lm->cmb->boneMatrices().size() : 0;
    if (boneId >= n) return;
    auto& v = bonePostRots()[modelId];
    if ((int)v.size() != n * 9) {
        v.assign(n * 9, 0.0f);
        for (int b = 0; b < n; b++) { v[b*9+0] = v[b*9+4] = v[b*9+8] = 1.0f; } // identity per bone
    }
    for (int k = 0; k < 9; k++) v[boneId * 9 + k] = mat9[k];
}

// #5 debug: dump per-bone vert influence + spatial extent so the wing bones can be identified by
// geometry (the parsed CMB has no bone names). Prints, per bone: id, parent, #verts weighted to it,
// and the mean local position of those verts (a wing bone's verts sit far out on one side in Z/X).
extern "C" void SoH3D_DumpBoneStats(int modelId) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb) {
        fprintf(stderr, "[BONESTATS] model %d not loaded\n", modelId);
        return;
    }
    const auto& bones = lm->cmb->bones();
    int n = (int)lm->cmb->boneMatrices().size();
    std::vector<int> vc(n, 0);
    std::vector<double> mx(n, 0), my(n, 0), mz(n, 0);
    for (const auto& g : lm->groups) {
        for (const auto& v : g.verts) {
            for (int k = 0; k < 4; k++) {
                if (v.weights[k] <= 0.0f) continue;
                int b = (int)(v.boneIds[k] + 0.5f);
                if (b < 0 || b >= n) continue;
                vc[b]++;
                mx[b] += v.pos[0]; my[b] += v.pos[1]; mz[b] += v.pos[2];
            }
        }
    }
    fprintf(stderr, "[BONESTATS] model %d bones=%d\n", modelId, n);
    for (const auto& bn : bones) {
        int id = bn.id;
        if (id < 0 || id >= n) continue;
        int c = vc[id];
        fprintf(stderr, "[BONESTATS]  bone %2d parent %2d verts %5d meanPos(%.1f,%.1f,%.1f) trans(%.1f,%.1f,%.1f)\n",
                id, bn.parent, c,
                c ? mx[id] / c : 0.0, c ? my[id] / c : 0.0, c ? mz[id] / c : 0.0,
                bn.trans[0], bn.trans[1], bn.trans[2]);
    }
    fflush(stderr);
}

// --- Posed-feet grounding for the player path (#29b "Link floats") ---------------------------
// The OoT3D Link CSABs carry absolute hip (bone 1) TRANSLATION tracks authored for the BOY rig;
// applied to ANY Link rig they lift the whole skeleton off the floor (the child floats ~930 local
// units ~= 40px on screen). The working N64-retarget path grounds precisely because it applies the
// rest (bind) translation and only REPLACES rotations -- it never sees those hip translations. The
// own-CSAB (linksrc 3ds) path applies the full CSAB, so it floats. We can't just drop the
// translation (the BOY rig's own run NEEDS its hip bob), so instead we measure the posed model's
// lowest VISIBLE vertex (its feet) each frame and offset the draw so the feet land on the actor's
// world pos.y -- the per-frame analogue of the auto path's bind-pose groundOffset. Gated per-model
// (only the player turns it on) so the per-vertex cost isn't paid on every NPC; needs the live mesh_id
// visibility mask so a hidden/unposed equipment variant (which sits at its bind ~-1325) can't skew it.
static std::unordered_map<int, char>& trackMinYFlags() {
    static std::unordered_map<int, char> m;
    return m;
}
static std::unordered_map<int, std::vector<std::array<float, 16>>>& lastSkin() {
    static std::unordered_map<int, std::vector<std::array<float, 16>>> m;
    return m;
}
// Cache this frame's skin matrices for a tracked model so SoH3D_PosedGroundOffset can recompute the
// posed feet position against the (later-known) mesh_id mask. No-op unless tracking is enabled.
static void cacheSkinForGround(int modelId, const std::vector<std::array<float, 16>>& sm) {
    auto it = trackMinYFlags().find(modelId);
    if (it == trackMinYFlags().end() || !it->second) return;
    lastSkin()[modelId] = sm;
}
extern "C" void SoH3D_SetTrackPosedMinY(int modelId, int enable) {
    trackMinYFlags()[modelId] = enable ? 1 : 0;
    if (!enable) lastSkin().erase(modelId);
}
// Model-local Y translation to add (innermost, pre-scale) so the posed model's lowest VISIBLE
// vertex lands on the actor's ground. midMask selects the drawn equipment/hand variant subset
// (same bit convention as SoH3D_GL_SetMidMask: bit i = mesh_id i visible; mesh_id<0 or >=64 always
// shown). Returns 0 if tracking wasn't enabled / no pose cached.
extern "C" float SoH3D_PosedGroundOffset(int modelId, unsigned long long midMask) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) return 0.0f;
    auto it = lastSkin().find(modelId);
    if (it == lastSkin().end() || it->second.empty()) return 0.0f;
    const auto& sm = it->second;
    const int n = (int)sm.size();
    float mn = 1e30f;
    for (const auto& g : lm->groups) {
        if (g.mesh_id >= 0 && g.mesh_id < 64 && !((midMask >> g.mesh_id) & 1ull)) continue;
        for (const auto& v : g.verts) {
            float y = 0.0f, wsum = 0.0f;
            for (int k = 0; k < 4; k++) {
                float w = v.weights[k];
                if (w <= 0.0f) continue;
                int b = (int)(v.boneIds[k] + 0.5f);
                if (b < 0 || b >= n) continue;
                const float* M = sm[b].data();
                y += w * (M[4] * v.pos[0] + M[5] * v.pos[1] + M[6] * v.pos[2] + M[7]);
                wsum += w;
            }
            if (wsum > 0.0f) { y /= wsum; if (y < mn) mn = y; }
        }
    }
    return (mn < 1e29f) ? -mn : 0.0f;
}

// Model-local position of a posed bone's ORIGIN this frame, recovered from the cached skin matrices
// (#6 held-actor attach). The animated bone-world matrix is aw[b] = skin[b]*bind[b], so the bone
// origin in model space is skin[b] applied to the bind-pose origin (bind[b]'s translation column).
// Returns 1 and writes outModelPos (3 floats) on success; 0 if no pose is cached / bone out of range.
// The caller lifts this through the actor world matrix (Matrix_MultVec3f) to get world space. Uses
// the SAME lastSkin cache the feet-grounding path already maintains, so any posing path (N64-retarget
// or CSAB) that ran cacheSkinForGround this frame exposes it. Requires SoH3D_SetTrackPosedMinY(1).
extern "C" int SoH3D_PosedBoneWorldPos(int modelId, int boneId, float* outModelPos) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb || !outModelPos) return 0;
    auto it = lastSkin().find(modelId);
    if (it == lastSkin().end() || it->second.empty()) return 0;
    const auto& sm = it->second;
    const auto& bind = lm->cmb->boneMatrices();
    if (boneId < 0 || (size_t)boneId >= sm.size() || (size_t)boneId >= bind.size()) return 0;
    const float* M = sm[boneId].data();
    const float* B = bind[boneId].data();
    const float bx = B[3], by = B[7], bz = B[11]; // bind-pose bone origin (row-major translation col)
    outModelPos[0] = M[0] * bx + M[1] * by + M[2] * bz + M[3];
    outModelPos[1] = M[4] * bx + M[5] * by + M[6] * bz + M[7];
    outModelPos[2] = M[8] * bx + M[9] * by + M[10] * bz + M[11];
    return 1;
}

// Pose-discontinuity scanner (anim QA tooling): the 3d3 named-CSAB path picks ONE csab at a phase and
// never blends morphs, so any transition that hard-cuts the pose shows as a per-bone rotation that JUMPS
// between consecutive frames far beyond what a continuous animation could produce. This compares the
// current cached pose (lastSkin) against the previous snapshot and returns the LARGEST per-bone rotation
// delta (degrees) plus that bone. Generic: works for ANY animation/transition driven through the model,
// needs no oracle. Orthonormalizes each bone's 3x3 (removing skin scale) then measures the relative
// rotation angle acos((tr(Ra^T Rb)-1)/2). First call after a reset returns 0 (no previous). Pair with
// the freeze/step harness + the action machine to sweep transitions and auto-flag pops. Uses lastSkin,
// so it requires SoH3D_SetTrackPosedMinY(1) on the model (the player path already enables it).
static std::unordered_map<int, std::vector<std::array<float, 16>>>& posePrev() {
    static std::unordered_map<int, std::vector<std::array<float, 16>>> m;
    return m;
}
static void orthoRows(const float* M, float r[3][3]) {
    // row-major 4x4 rotation rows (v' = M*v): r0..r2; Gram-Schmidt to a pure rotation.
    float a[3] = { M[0], M[1], M[2] }, b[3] = { M[4], M[5], M[6] }, c[3] = { M[8], M[9], M[10] };
    auto norm = [](float* v) { float n = std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); if (n>1e-8f){v[0]/=n;v[1]/=n;v[2]/=n;} };
    auto dot = [](const float* u, const float* v) { return u[0]*v[0]+u[1]*v[1]+u[2]*v[2]; };
    norm(a);
    float pb = dot(a, b); b[0]-=pb*a[0]; b[1]-=pb*a[1]; b[2]-=pb*a[2]; norm(b);
    c[0]=a[1]*b[2]-a[2]*b[1]; c[1]=a[2]*b[0]-a[0]*b[2]; c[2]=a[0]*b[1]-a[1]*b[0]; // c = a x b
    for (int k=0;k<3;k++){ r[0][k]=a[k]; r[1][k]=b[k]; r[2][k]=c[k]; }
}
extern "C" float SoH3D_PoseDiscontinuity(int modelId, int* outBone) {
    if (outBone) *outBone = -1;
    auto it = lastSkin().find(modelId);
    if (it == lastSkin().end() || it->second.empty()) return 0.0f;
    const auto& cur = it->second;
    auto& prev = posePrev()[modelId];
    float maxDeg = 0.0f; int maxBone = -1;
    if (prev.size() == cur.size()) {
        for (size_t b = 0; b < cur.size(); b++) {
            float Ra[3][3], Rb[3][3];
            orthoRows(prev[b].data(), Ra);
            orthoRows(cur[b].data(), Rb);
            // tr(Ra^T * Rb) = sum_ij Ra[i][j]*Rb[i][j]  (Ra rows are basis vectors)
            float tr = 0.0f;
            for (int i=0;i<3;i++) for (int j=0;j<3;j++) tr += Ra[i][j]*Rb[i][j];
            float cosA = (tr - 1.0f) * 0.5f;
            if (cosA > 1.0f) cosA = 1.0f; if (cosA < -1.0f) cosA = -1.0f;
            float deg = std::acos(cosA) * (180.0f / 3.14159265358979f);
            if (deg > maxDeg) { maxDeg = deg; maxBone = (int)b; }
        }
    }
    prev = cur; // snapshot for next call
    if (outBone) *outBone = maxBone;
    return maxDeg;
}
extern "C" void SoH3D_PoseScanReset(int modelId) { posePrev().erase(modelId); }

// Look up the per-model procedural bone-rotation deltas (cucco flap, euler pre-add), if any.
static void getBoneRotDeltas(int modelId, const float** outDrot, int* outDcount) {
    *outDrot = nullptr; *outDcount = 0;
    auto it = boneRotDeltas().find(modelId);
    if (it != boneRotDeltas().end() && !it->second.empty()) {
        *outDrot = it->second.data();
        *outDcount = (int)it->second.size() / 3;
    }
}
// Look up the per-model per-bone post-rotation matrices (head/torso track, MTXMODE_APPLY), if any.
static void getBonePostRots(int modelId, const float** outPost, int* outCount) {
    *outPost = nullptr; *outCount = 0;
    auto it = bonePostRots().find(modelId);
    if (it != bonePostRots().end() && !it->second.empty()) {
        *outPost = it->second.data();
        *outCount = (int)it->second.size() / 9;
    }
}

// Common tail for the CSAB sample paths: cache for grounding, then upload bind + skin to GL. The
// GL layer recovers the animated bone-world transform (skin*bind) and interpolates the pose RIGIDLY
// between logic frames — interpolating the skin matrices directly shatters large per-frame rotations.
static void uploadSkin(int modelId, LoadedModel* lm, std::vector<std::array<float, 16>>& sm) {
    cacheSkinForGround(modelId, sm); // posed-feet grounding for the player path (#29b)
    const auto& bind = lm->cmb->boneMatrices();
    SoH3D_GL_SetBoneBind(modelId, bind.empty() ? nullptr : bind.front().data(), (int)bind.size());
    // vector<array<float,16>> is contiguous -> hand the renderer a flat float buffer.
    SoH3D_GL_SetBones(modelId, sm.empty() ? nullptr : sm.front().data(), (int)sm.size());
}

// --- Resolved-pose geometry capture (anim-parity harness, #117) ---------------------------------
// Dumps the ACTUAL resolved per-bone skin matrices (the geometry the renderer draws) for one tracked
// model, per draw, tagged with the resolved CSAB name + the REAL playhead frame (the free-run
// accumulator, not the dead skelAnime.curFrame). This is the SoH3D side of the direct-vs-oracle
// per-frame diff: it answers "does the pose actually cycle / what pose is on screen this frame".
// Generic (any auto-model). Arm via the REPL `skindump` (resolves Link's modelId). Each row is one
// bone's animated bone-WORLD matrix aw = skin·bind (row-major top 3 rows m0..m11): the rotation 3x3
// is (m0..m2 / m4..m6 / m8..m10) and the bone WORLD POSITION is (m3,m7,m11). Bone world positions are
// the quantity the parity sweep Procrustes-aligns against the oracle's live bone matrices
// (oot3d-decomp link_skel_live), so a divergent state (e.g. static legs sliding) shows as per-bone
// residual. A frozen pose still shows as identical rows across caps.
static FILE* gSkinDumpFile = nullptr;
static int gSkinDumpModel = -1;
static int gSkinDumpRemaining = 0;
static int gSkinDumpCap = 0;
extern "C" void SoH3D_SkinDumpArm(int modelId, const char* path, int frames) {
    if (gSkinDumpFile) { fclose(gSkinDumpFile); gSkinDumpFile = nullptr; }
    gSkinDumpFile = fopen(path, "w");
    if (!gSkinDumpFile) return;
    fprintf(gSkinDumpFile, "# resolved CSAB pose capture: animated bone-WORLD matrix aw=skin*bind,\n");
    fprintf(gSkinDumpFile, "# row-major top 3 rows; bone world pos=(m3,m7,m11). Parity vs oracle link_skel_live.\n");
    fprintf(gSkinDumpFile, "cap,anim,frame,bone,m0,m1,m2,m3,m4,m5,m6,m7,m8,m9,m10,m11\n");
    gSkinDumpModel = modelId; gSkinDumpRemaining = frames; gSkinDumpCap = 0;
}
// `aw` is the animated bone-WORLD matrix straight from Csab::animatedBoneWorld (recursive parent
// multiply) — NOT reconstructed via skin*bind, which is lossy when bind carries scale (matInverse
// is approximate there). Bone world position = (m3,m7,m11).
static void skinDumpWrite(int modelId, const char* animName, float frame,
                          const std::vector<std::array<float, 16>>& aw) {
    if (!gSkinDumpFile || modelId != gSkinDumpModel || gSkinDumpRemaining <= 0) return;
    for (int b = 0; b < (int)aw.size(); b++) {
        const float* M = aw[b].data();
        fprintf(gSkinDumpFile, "%d,%s,%.3f,%d,%.4f,%.4f,%.4f,%.2f,%.4f,%.4f,%.4f,%.2f,%.4f,%.4f,%.4f,%.2f\n",
                gSkinDumpCap, animName ? animName : "(null)", frame, b,
                M[0], M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11]);
    }
    gSkinDumpCap++;
    if (--gSkinDumpRemaining == 0) { fclose(gSkinDumpFile); gSkinDumpFile = nullptr; gSkinDumpModel = -1; }
}

void SoH3D_UpdateAnim(int modelId, const char* animName, float frame) {
    if (!animName || !*animName) { SoH3D_GL_SetBones(modelId, nullptr, 0); return; }
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb || !lm->zar) return;

    SoH3D::Csab* anim = getCsab(lm, animName);
    if (!anim) { SoH3D_GL_SetBones(modelId, nullptr, 0); return; }

    std::vector<std::array<float, 16>> sm;
    const float* drot = nullptr; int dcount = 0;
    const float* post = nullptr; int pcount = 0;
    getBoneRotDeltas(modelId, &drot, &dcount);
    getBonePostRots(modelId, &post, &pcount);
    anim->skinMatrices(*lm->cmb, frame, sm, drot, dcount, post, pcount);
    if (gSkinDumpFile && modelId == gSkinDumpModel) {
        std::vector<std::array<float, 16>> aw;
        anim->animatedBoneWorld(*lm->cmb, frame, aw, drot, dcount, post, pcount);
        skinDumpWrite(modelId, animName, frame, aw);
    }
    uploadSkin(modelId, lm, sm);
}

// MORPH variant of SoH3D_UpdateAnim: cross-fade the INCOMING clip (inName@fIn) toward the frozen
// OUTGOING clip (outName@fOut) by `weight` (= N64 morphWeight, 1->0 over the transition). Same
// model, same upload tail. If the outgoing CSAB can't be resolved, falls back to a plain incoming
// sample (no morph) rather than dropping the pose.
static void SoH3D_UpdateAnimMorph(int modelId, const char* inName, float fIn, const char* outName,
                                  float fOut, float weight) {
    if (!inName || !*inName) { SoH3D_GL_SetBones(modelId, nullptr, 0); return; }
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb || !lm->zar) return;
    SoH3D::Csab* in = getCsab(lm, inName);
    if (!in) { SoH3D_GL_SetBones(modelId, nullptr, 0); return; }
    SoH3D::Csab* out = (outName && *outName) ? getCsab(lm, outName) : nullptr;
    const float* drot = nullptr; int dcount = 0;
    const float* post = nullptr; int pcount = 0;
    getBoneRotDeltas(modelId, &drot, &dcount);
    getBonePostRots(modelId, &post, &pcount);
    std::vector<std::array<float, 16>> sm;
    if (out) {
        in->skinMatricesMorph(*lm->cmb, fIn, *out, fOut, weight, sm, drot, dcount, post, pcount);
    } else {
        in->skinMatrices(*lm->cmb, fIn, sm, drot, dcount, post, pcount); // outgoing unresolved -> no blend
    }
    if (gSkinDumpFile && modelId == gSkinDumpModel) {
        std::vector<std::array<float, 16>> aw;
        if (out) in->animatedBoneWorldMorph(*lm->cmb, fIn, *out, fOut, weight, aw, drot, dcount, post, pcount);
        else in->animatedBoneWorld(*lm->cmb, fIn, aw, drot, dcount, post, pcount);
        skinDumpWrite(modelId, inName, fIn, aw);
    }
    uploadSkin(modelId, lm, sm);
}

// Drive an auto-replaced model by its OWN OoT3D CSAB (animName). Two playhead modes:
//   PHASE-LOCK (n64AnimLength>4): the OoT3D CSAB is driven at the SAME fractional progress as the
//     actor's live N64 animation (csab_frame = (n64CurFrame/n64AnimLength) * csab_duration). This
//     is the fix for "OoT3D anims too fast" — the CSAB free-ran at a fixed `rate` regardless of how
//     fast the N64 game logic was actually advancing the anim, so a slow N64 walk looked sped-up.
//     Locking to N64 progress makes the OoT3D motion match the N64 motion's tempo exactly.
//   FREE-RUN (stub idle, n64AnimLength<=4, or duration unknown): self-managed per-model playhead at
//     `rate` frames/draw. N64 idles are often 2-frame fidget stubs with no meaningful progress to
//     lock to (see memory n64-idle-stub-no-phaselock), so we free-run the full OoT3D idle instead.
// animName==NULL -> bind pose. The CSAB wraps the frame internally (Csab::animFrame REPEAT).
//
// MORPH (#8/#86, keystone fix #2): `morphWeight` is the live N64 skelAnime->morphWeight (1.0 on the
// transition frame, ramping linearly to 0). When a transition is detected (the resolved CSAB name
// changes while morphWeight>0) we FREEZE the outgoing clip+frame and, while morphWeight>0, cross-fade
// the new clip toward that frozen pose — exactly the N64 SkelAnime morph model (docs/anim_system.md
// "THE MORPH"). Without this the auto/CSAB path hard-cuts every transition (1-frame arm/limb pops).
// gSoH3dMorph (REPL `morph 0|1`, env SOH3D_MORPH default on) gates it for A/B verification.
int gSoH3dMorph = -1;
void SoH3D_UpdateAnimAuto(int modelId, const char* animName, float rate, float n64CurFrame,
                          float n64AnimLength, float morphWeight) {
    static std::unordered_map<int, float> frames;
    static std::unordered_map<int, std::string> lastCsab; // per-model: which CSAB the playhead is on
    static std::unordered_map<int, float> lastFrame;      // last incoming frame rendered (freeze src)
    static std::unordered_map<int, std::string> morphOut; // frozen outgoing CSAB during a morph
    static std::unordered_map<int, float> morphOutFrame;  // frozen outgoing frame
    if (!animName || !*animName) {
        frames.erase(modelId); lastCsab.erase(modelId); lastFrame.erase(modelId);
        morphOut.erase(modelId); morphOutFrame.erase(modelId);
        SoH3D_UpdateAnim(modelId, nullptr, 0); return;
    }
    if (gSoH3dMorph < 0) {
        const char* v = getenv("SOH3D_MORPH");
        gSoH3dMorph = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    if (!gSoH3dMorph) morphWeight = 0.0f;

    // --- incoming frame f (phase-lock to the N64 anim's progress, else free-run) ---
    LoadedModel* lm = loadModel(modelId);
    float dur = 0.0f;
    if (lm && lm->ok && lm->cmb && lm->zar) {
        SoH3D::Csab* c = getCsab(lm, animName);
        if (c) dur = (float)c->duration();
    }
    bool locked = (n64AnimLength > 4.0f && n64CurFrame >= 0.0f && dur > 0.0f);
    auto lcIt = lastCsab.find(modelId);
    bool csabChanged = (lcIt == lastCsab.end()) || (lcIt->second != animName);
    float f;
    if (locked) {
        float phase = n64CurFrame / n64AnimLength;
        phase -= std::floor(phase); // wrap into [0,1)
        f = phase * dur;
        frames[modelId] = f; // keep the free-run playhead in sync for a later mode switch
    } else {
        // FREE-RUN. Restart from 0 whenever the CSAB changes, so a one-shot (a wave, a hand-off)
        // plays from its start instead of resuming at the previous anim's frame.
        float& pf = frames[modelId];
        if (csabChanged) pf = 0.0f;
        f = pf;
        pf += rate;
    }

    // --- morph bookkeeping: on a real transition, freeze the outgoing clip at its last frame ---
    if (csabChanged) {
        if (morphWeight > 0.0f && lcIt != lastCsab.end()) {
            morphOut[modelId] = lcIt->second; // the clip we're leaving
            auto lfIt = lastFrame.find(modelId);
            morphOutFrame[modelId] = (lfIt != lastFrame.end()) ? lfIt->second : 0.0f;
        } else {
            morphOut.erase(modelId); morphOutFrame.erase(modelId); // hard cut / first anim
        }
    }
    if (morphWeight <= 0.0f) { morphOut.erase(modelId); morphOutFrame.erase(modelId); }
    lastCsab[modelId] = animName;
    lastFrame[modelId] = f;

    auto moIt = morphOut.find(modelId);
    if (moIt != morphOut.end() && morphWeight > 0.0f) {
        SoH3D_UpdateAnimMorph(modelId, animName, f, moIt->second.c_str(), morphOutFrame[modelId],
                              morphWeight);
    } else {
        SoH3D_UpdateAnim(modelId, animName, f);
    }
}

// TWO-SOURCE per-limb blend (#85 carry-WALK): drive the LOWER body from `lowerAnim` (the locomotion
// CSAB, free-run by `lowerRate` frames/draw exactly like the loco branch of SoH3D_UpdateAnimAuto —
// the run/walk cycle's curFrame is dead during steady movement, so we advance by ground speed) and
// the UPPER body from `upperAnim` (the carry pose CSAB). For each bone, `upperMask[id] != 0` selects
// the upper clip (mask = the OoT3D analogue of sUpperBodyLimbCopyMap; see soh3d_link.cpp). The upper
// clip is phase-locked to its N64 progress when meaningful (upperAnimLength > 4), else free-run, so a
// real carry hold (carryB_wait, near-static) plays faithfully without a leg cycle bleeding into it.
// This replaces the carry-walk N64-retarget detour in the 3DS-CSAB Link path (the last retarget dep).
void SoH3D_UpdateAnimTwoSource(int modelId, const char* lowerAnim, float lowerRate,
                               const char* upperAnim, float upperCurFrame, float upperAnimLength,
                               const unsigned char* upperMask, int maskCount) {
    static std::unordered_map<int, float> lowerFrames;     // per-model loco free-run playhead
    static std::unordered_map<int, std::string> lastLower; // last lower CSAB (restart on change)
    static std::unordered_map<int, float> upperFrames;     // per-model upper free-run playhead (fallback)
    static std::unordered_map<int, std::string> lastUpper;
    if (!lowerAnim || !*lowerAnim || !upperAnim || !*upperAnim) {
        lowerFrames.erase(modelId); lastLower.erase(modelId);
        upperFrames.erase(modelId); lastUpper.erase(modelId);
        SoH3D_UpdateAnim(modelId, nullptr, 0); return;
    }
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb || !lm->zar) return;
    SoH3D::Csab* lower = getCsab(lm, lowerAnim);
    SoH3D::Csab* upper = getCsab(lm, upperAnim);
    if (!lower || !upper) { SoH3D_GL_SetBones(modelId, nullptr, 0); return; }

    // LOWER: free-run by ground speed (legs cycle), restart on a CSAB change so a fresh clip plays
    // from frame 0 rather than resuming the previous one's phase.
    {
        auto llIt = lastLower.find(modelId);
        if (llIt == lastLower.end() || llIt->second != lowerAnim) lowerFrames[modelId] = 0.0f;
        lastLower[modelId] = lowerAnim;
    }
    float fLower = lowerFrames[modelId];
    lowerFrames[modelId] += lowerRate;

    // UPPER: phase-lock to the N64 carry anim's progress when it carries real progress, else free-run
    // (matching SoH3D_UpdateAnimAuto's lock/free choice).
    float fUpper;
    float upDur = (float)upper->duration();
    if (upperAnimLength > 4.0f && upperCurFrame >= 0.0f && upDur > 0.0f) {
        float phase = upperCurFrame / upperAnimLength;
        phase -= std::floor(phase);
        fUpper = phase * upDur;
        upperFrames[modelId] = fUpper;
        lastUpper[modelId] = upperAnim;
    } else {
        auto luIt = lastUpper.find(modelId);
        if (luIt == lastUpper.end() || luIt->second != upperAnim) upperFrames[modelId] = 0.0f;
        lastUpper[modelId] = upperAnim;
        fUpper = upperFrames[modelId];
        upperFrames[modelId] += gSoH3dAnimRate;
    }

    const float* drot = nullptr; int dcount = 0;
    const float* post = nullptr; int pcount = 0;
    getBoneRotDeltas(modelId, &drot, &dcount);
    getBonePostRots(modelId, &post, &pcount);
    std::vector<std::array<float, 16>> sm;
    lower->skinMatricesTwoSource(*lm->cmb, fLower, *upper, fUpper, upperMask, maskCount, sm, drot,
                                 dcount, post, pcount);
    if (gSkinDumpFile && modelId == gSkinDumpModel) {
        std::vector<std::array<float, 16>> aw;
        lower->animatedBoneWorldTwoSource(*lm->cmb, fLower, *upper, fUpper, upperMask, maskCount, aw,
                                          drot, dcount, post, pcount);
        skinDumpWrite(modelId, lowerAnim, fLower, aw);
    }
    uploadSkin(modelId, lm, sm);
}

} // extern "C"
