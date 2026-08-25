// Zelda3D animation execution — resolve authored CSABs or N64 joint tables to skin matrices and
// upload the resulting pose. Uses the model core via zelda3d_model_internal.h.
#include "../model/zelda3d_model_internal.h"
#include "authored_playback.h"
#include "pose_evaluation_internal.h"
#include "pose_inspection_internal.h"
#include "pose_tracking_internal.h"
#include "skeleton_draw_bridge.h"
#include "asset/cmb.h"
#include "asset/csab.h"
#include "asset/mat4.h"
#include "fast/zelda3d_pose.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <utility>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Matches the struct in zelda3d.h (C ABI). Per OoT3D bone: which N64 jointRots index drives it
// and HOW. mode 0=rest (keep CMB rest rot), 1=replace (local rot := N64 rot), 2=left (C·R_n64),
// 3=right (R_n64·C). C is a row-major 3x3 constant rest-frame correction for bones whose OoT3D
// rest frame diverges from the N64 limb's (Grezzo re-rigged Link's spine/arms). See
// tools/zelda3d_link_retarget_derive.py and [[zelda3d-n64anim-retarget]].
typedef struct {
    signed char limb;
    unsigned char mode; // 0 rest, 1 replace, 2 left C·R, 3 right R·C, 4 two-sided C·R·C2
    float C[9];
    float C2[9]; // right factor for mode 4 (identity otherwise)
} Zelda3dBoneCorr;

// Get-or-load the parsed CSAB for `animName` (base name or full "Anim/<n>.csab"),
// caching it on the model. Returns nullptr if missing/unparseable (logged once via
// the cached null entry). Shared by the frame- and phase-based update entry points.
static Zelda3D::Csab* getCsab(LoadedModel* lm, const char* animName) {
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
        const Zelda3D::ZarFile* af = nullptr;
        for (const auto& f : lm->zar->files())
            if (f.name == full) {
                af = &f;
                break;
            }
        if (!af && !verbatim) { // basename fallback: "<base>.csab" anywhere in the zar (link boy/child/anim)
            // Try the verbatim base first, then the child "cl_" prefix: Grezzo prefixes some child-age
            // link anims with cl_ (e.g. boy dm_Tbox_open <-> child cl_dm_Tbox_open), so a single
            // basename map entry resolves for either age. (See tools/gen_player_animmap.py resolves_in.)
            const std::string suffixes[2] = { "/" + nm + ".csab", "/cl_" + nm + ".csab" };
            for (const std::string& suffix : suffixes) {
                for (const auto& f : lm->zar->files()) {
                    if (f.name.size() >= suffix.size() &&
                        f.name.compare(f.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                        af = &f;
                        full = f.name; // cache under the real path so a re-resolve hits directly
                        break;
                    }
                }
                if (af)
                    break;
            }
            auto it2 = lm->anims.find(full); // the resolved path may already be cached
            if (it2 != lm->anims.end())
                return it2->second.get();
        }
        std::unique_ptr<Zelda3D::Csab> csab;
        if (af) {
            csab = std::make_unique<Zelda3D::Csab>(lm->zar->read(*af));
            if (!csab->ok()) {
                fprintf(stderr, "[Zelda3D] Csab %s: %s\n", full.c_str(), csab->error().c_str());
                csab.reset();
            }
        } else {
            fprintf(stderr, "[Zelda3D] anim not found: %s\n", full.c_str());
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
// contorts the pose. Convention derived QUANTITATIVELY (tools/zelda3d_anim_derive.py: diff CSAB
// ge1_s_wait skin matrices vs N64-joint-driven ones -> struct=replace, euler order ZYX wins
// over every compose variant). L = T(rest)*Rz*Ry*Rx(n64)*S(rest); skin = animWorld*bindInverse.
extern "C" void Zelda3D_UpdateAnimN64Mapped(int modelId, const int16_t* jointRots, int rotCount,
                                            const signed char* boneToLimb, int mapCount);

extern "C" void Zelda3D_UpdateAnimN64(int modelId, const int16_t* jointRots, int rotCount) {
    Zelda3D_UpdateAnimN64Mapped(modelId, jointRots, rotCount, nullptr, 0);
}

// As Zelda3D_UpdateAnimN64, but with an explicit OoT3D-bone -> N64-limb correspondence
// (`boneToLimb`, indexed by bone id; -1 = no live joint -> keep rest). NULL map = identity
// (bone i <- limb i), the same-rig assumption. The map is the precomputed correspondence
// (tools/zelda3d_skel_match.py -> zelda3d_bonemap.inc), needed for rigs whose topology differs
// from the N64 skeleton (OoT3D inserts root/reorient bones). See PROGRESS "replace ALL chars".
extern "C" void Zelda3D_UpdateAnimN64Mapped(int modelId, const int16_t* jointRots, int rotCount,
                                            const signed char* boneToLimb, int mapCount) {
    using namespace Zelda3D;
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb) {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }
    const auto& bones = lm->cmb->bones();
    const auto& bind = lm->cmb->boneMatrices();
    const float kBinangToRad = 3.14159265358979f / 32768.0f;

    std::vector<Mat4> aw(bind.size(), matId());
    std::vector<char> done(bind.size(), 0);
    std::vector<const CmbBone*> byId(bind.size(), nullptr);
    for (const auto& bn : bones)
        if (bn.id >= 0 && (size_t)bn.id < byId.size())
            byId[bn.id] = &bn;

    std::function<Mat4(int)> world = [&](int id) -> Mat4 {
        if (id < 0 || (size_t)id >= aw.size() || !byId[id])
            return matId();
        if (done[id])
            return aw[id];
        const CmbBone* bn = byId[id];
        Mat4 L = matT(bn->trans[0], bn->trans[1], bn->trans[2]);
        // limb = the N64 limb whose rotation drives this OoT3D bone: the precomputed map if
        // present, else identity (bone id == limb index).
        int limb = boneToLimb ? (id < mapCount ? (int)boneToLimb[id] : -1) : id;
        if (limb >= 0 && limb < rotCount) {
            // Use the N64 joint rotation AS the bone's local rotation (replacing the CMB rest
            // rotation), in csab.cpp's Rz*Ry*Rx order. The jointTable already carries the full
            // limb orientation, so composing it with the rest rotation double-applies and
            // contorts (verified by tools/zelda3d_anim_derive.py: replace beats compose).
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
    for (const auto& bn : bones)
        world(bn.id);

    std::vector<std::array<float, 16>> sm(bind.size());
    for (size_t id = 0; id < bind.size(); id++)
        sm[id] = matMul(aw[id], matInverse(bind[id]));
    Zelda3D_CacheTrackedPose(modelId, sm); // posed-feet grounding for the player path (#29b)
    // Upload bind for correct rigid pose interpolation (see Zelda3D_UpdateAnim / interpSkinPose).
    Zelda3D_GL_SetBoneBind(modelId, bind.empty() ? nullptr : bind.front().data(), (int)bind.size());
    Zelda3D_GL_SetBones(modelId, sm.empty() ? nullptr : sm.front().data(), (int)sm.size());
}

// As Zelda3D_UpdateAnimN64Mapped, but each OoT3D bone carries a per-bone CORRECTION (Zelda3dBoneCorr,
// indexed by bone id): mode 1 = pure "replace" (local rot := N64 rot, the same-rest case that works
// for Link's legs/head); mode 2/3 = apply a constant rest-frame correction C on the left (C·R_n64)
// or right (R_n64·C) for bones whose OoT3D rest diverges from the N64 limb's (Grezzo re-rigged
// Link's spine/upper arms — see tools/zelda3d_link_retarget_derive.py). mode 0 / limb<0 = keep the
// CMB rest pose. Same FK + skin-matrix tail as the Mapped variant.
extern "C" void Zelda3D_UpdateAnimN64Corr(int modelId, const int16_t* jointRots, int rotCount,
                                          const Zelda3dBoneCorr* corr, int corrCount) {
    using namespace Zelda3D;
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb) {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }
    const auto& bones = lm->cmb->bones();
    const auto& bind = lm->cmb->boneMatrices();
    const float kBinangToRad = 3.14159265358979f / 32768.0f;

    auto corrMat = [](const float* c) -> Mat4 {
        Mat4 m = matId();
        m[0] = c[0];
        m[1] = c[1];
        m[2] = c[2];
        m[4] = c[3];
        m[5] = c[4];
        m[6] = c[5];
        m[8] = c[6];
        m[9] = c[7];
        m[10] = c[8];
        return m;
    };

    std::vector<Mat4> aw(bind.size(), matId());
    std::vector<char> done(bind.size(), 0);
    std::vector<const CmbBone*> byId(bind.size(), nullptr);
    for (const auto& bn : bones)
        if (bn.id >= 0 && (size_t)bn.id < byId.size())
            byId[bn.id] = &bn;

    std::function<Mat4(int)> world = [&](int id) -> Mat4 {
        if (id < 0 || (size_t)id >= aw.size() || !byId[id])
            return matId();
        if (done[id])
            return aw[id];
        const CmbBone* bn = byId[id];
        Mat4 L = matT(bn->trans[0], bn->trans[1], bn->trans[2]);
        const Zelda3dBoneCorr* c = (corr && id < corrCount) ? &corr[id] : nullptr;
        int limb = c ? c->limb : -1;
        int mode = c ? c->mode : 0;
        if (mode >= 1 && limb >= 0 && limb < rotCount) {
            float rx = jointRots[limb * 3 + 0] * kBinangToRad;
            float ry = jointRots[limb * 3 + 1] * kBinangToRad;
            float rz = jointRots[limb * 3 + 2] * kBinangToRad;
            Mat4 R = matMul(matMul(matRz(rz), matRy(ry)), matRx(rx)); // N64 local rotation (Rz·Ry·Rx)
            if (mode == 2)
                R = matMul(corrMat(c->C), R); // left:  C·R_n64
            else if (mode == 3)
                R = matMul(R, corrMat(c->C)); // right: R_n64·C
            else if (mode == 4)
                R = matMul(matMul(corrMat(c->C), R), corrMat(c->C2)); // C·R·C2
            else if (mode == 5) {
                // Conjugation C·R·C⁻¹ (C⁻¹ = Cᵀ for a rotation): a change of basis for the rest-frame
                // discrepancy. Unlike mode 2/3 (a one-sided constant that's only right near the tuned
                // pose), this transforms R itself, so a single hand-tuned C holds across the FULL pose
                // range — idle, walk AND the arms-overhead carry pose (#6). Tune just C.
                Mat4 C = corrMat(c->C);
                Mat4 Ci = matId();
                Ci[0] = C[0];
                Ci[1] = C[4];
                Ci[2] = C[8];
                Ci[4] = C[1];
                Ci[5] = C[5];
                Ci[6] = C[9];
                Ci[8] = C[2];
                Ci[9] = C[6];
                Ci[10] = C[10];
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
    for (const auto& bn : bones)
        world(bn.id);

    std::vector<std::array<float, 16>> sm(bind.size());
    for (size_t id = 0; id < bind.size(); id++)
        sm[id] = matMul(aw[id], matInverse(bind[id]));
    Zelda3D_CacheTrackedPose(modelId, sm); // posed-feet grounding for the player path (#29b)
    // Upload bind for correct rigid pose interpolation (see Zelda3D_UpdateAnim / interpSkinPose).
    Zelda3D_GL_SetBoneBind(modelId, bind.empty() ? nullptr : bind.front().data(), (int)bind.size());
    Zelda3D_GL_SetBones(modelId, sm.empty() ? nullptr : sm.front().data(), (int)sm.size());
}

extern "C" {

// Set the model's GPU skinning pose to `animName` (CSAB base name, e.g. "ge1_s_wait")
// at `frame`. animName==NULL/"" resets to the bind pose. Loads the model + caches the
// parsed CSAB on first use; recomputes skin matrices each call (cheap: <=32 bones).
// Call once per game frame before the Zelda3D draw. Safe to call repeatedly.
// Per-model procedural per-bone local-rotation deltas (radians, 3 per bone id), set by the auto
// retarget path from an N64 OverrideLimbDraw probe (e.g. the cucco wing-flap) and consumed by the
// next Zelda3D_UpdateAnim for that model. Empty = no delta (the common case; static-pose unchanged).
static std::unordered_map<int, std::vector<float>>& boneRotDeltas() {
    static std::unordered_map<int, std::vector<float>> m;
    return m;
}
extern "C" void Zelda3D_ClearBoneRotDeltas(int modelId) {
    boneRotDeltas().erase(modelId);
}
extern "C" void Zelda3D_SetBoneRotDelta(int modelId, int boneId, float rx, float ry, float rz) {
    if (boneId < 0)
        return;
    LoadedModel* lm = loadModel(modelId);
    int n = (lm && lm->ok && lm->cmb) ? (int)lm->cmb->boneMatrices().size() : 0;
    if (boneId >= n)
        return;
    auto& v = boneRotDeltas()[modelId];
    if ((int)v.size() != n * 3)
        v.assign(n * 3, 0.0f);
    v[boneId * 3 + 0] = rx;
    v[boneId * 3 + 1] = ry;
    v[boneId * 3 + 2] = rz;
}

// Per-model anim-translation scale: rig-vs-clip translation-space ratio for ANIMATED translation
// tracks (Csab::sampleLocalTRS animTransScale). The player path sets this to the N64 age root scale
// (child 0.64, adult 1.0 — z_player_lib.c Player_OverrideLimbDrawGameplayDefault; Grezzo kept the
// 0.64 literal on 3DS: FUN_002bc768 DAT_002bc8b8). Default 1.0 for every other model.
static std::unordered_map<int, Zelda3D::RootMotion>& rootMotions() {
    static std::unordered_map<int, Zelda3D::RootMotion> m;
    return m;
}
extern "C" void Zelda3D_SetAnimTransScale(int modelId, float scale) {
    rootMotions()[modelId].transScale = scale;
}
// ROOT-MOTION PIN (Csab::RootMotion pinBone/pinMask): while the engine is consuming this model's
// clip root translation into the actor position, the pinned components must be drawn from the
// rig's REST translation, not the clip track — the mirror of N64
// SkelAnime_UpdateTranslation writing `jointTable[0].c = baseTransl.c` (z_skelanime.c:2025-2040).
// mask bit0/1/2 = x/y/z; bone < 0 or mask 0 clears the pin.
extern "C" void Zelda3D_SetAnimRootPin(int modelId, int boneId, unsigned mask) {
    auto& rm = rootMotions()[modelId];
    rm.pinBone = (mask != 0) ? boneId : -1;
    rm.pinMask = (boneId >= 0) ? mask : 0u;
}
static Zelda3D::RootMotion getAnimTransScale(int modelId) {
    auto it = rootMotions().find(modelId);
    return it == rootMotions().end() ? Zelda3D::RootMotion{} : it->second;
}

// Per-model per-bone POST-rotation matrix (row-major 3x3, 9 floats/bone) post-multiplied onto the
// bone's animated local rotation by the CSAB skinner — the OoT3D actor OverrideLimbDraw MTXMODE_APPLY
// channel (En_Ko/En_Sa head/torso tracking). Distinct from boneRotDeltas (euler pre-add, cucco flap):
// a post-multiply in the bone's local frame matches OoT3D's matrix-apply and propagates to children.
static std::unordered_map<int, std::vector<float>>& bonePostRots() {
    static std::unordered_map<int, std::vector<float>> m;
    return m;
}
extern "C" void Zelda3D_ClearBonePostRots(int modelId) {
    bonePostRots().erase(modelId);
}
extern "C" void Zelda3D_SetBonePostRot(int modelId, int boneId, const float* mat9) {
    if (boneId < 0 || !mat9)
        return;
    LoadedModel* lm = loadModel(modelId);
    int n = (lm && lm->ok && lm->cmb) ? (int)lm->cmb->boneMatrices().size() : 0;
    if (boneId >= n)
        return;
    auto& v = bonePostRots()[modelId];
    if ((int)v.size() != n * 9) {
        v.assign(n * 9, 0.0f);
        for (int b = 0; b < n; b++) {
            v[b * 9 + 0] = v[b * 9 + 4] = v[b * 9 + 8] = 1.0f;
        } // identity per bone
    }
    for (int k = 0; k < 9; k++)
        v[boneId * 9 + k] = mat9[k];
}

// #5 debug: dump per-bone vert influence + spatial extent so the wing bones can be identified by
// geometry (the parsed CMB has no bone names). Prints, per bone: id, parent, #verts weighted to it,
// and the mean local position of those verts (a wing bone's verts sit far out on one side in Z/X).

// Look up the per-model procedural bone-rotation deltas (cucco flap, euler pre-add), if any.
static void getBoneRotDeltas(int modelId, const float** outDrot, int* outDcount) {
    *outDrot = nullptr;
    *outDcount = 0;
    auto it = boneRotDeltas().find(modelId);
    if (it != boneRotDeltas().end() && !it->second.empty()) {
        *outDrot = it->second.data();
        *outDcount = (int)it->second.size() / 3;
    }
}
// Look up the per-model per-bone post-rotation matrices (head/torso track, MTXMODE_APPLY), if any.
static void getBonePostRots(int modelId, const float** outPost, int* outCount) {
    *outPost = nullptr;
    *outCount = 0;
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
    Zelda3D_CacheTrackedPose(modelId, sm); // posed-feet grounding for the player path (#29b)
    const auto& bind = lm->cmb->boneMatrices();
    Zelda3D_GL_SetBoneBind(modelId, bind.empty() ? nullptr : bind.front().data(), (int)bind.size());
    // vector<array<float,16>> is contiguous -> hand the renderer a flat float buffer.
    Zelda3D_GL_SetBones(modelId, sm.empty() ? nullptr : sm.front().data(), (int)sm.size());
}

// --- Resolved-pose geometry capture (anim-parity harness, #117) ---------------------------------
// Dumps the ACTUAL resolved per-bone skin matrices (the geometry the renderer draws) for one tracked
// model, per draw, tagged with the resolved CSAB name + the REAL playhead frame (the free-run
// accumulator, not the dead skelAnime.curFrame). This is the Zelda3D side of the direct-vs-oracle
// per-frame diff: it answers "does the pose actually cycle / what pose is on screen this frame".

// Bind-pose (no CSAB) skinning with the procedural per-bone channels (boneRotDelta / bonePostRot)
// applied. For statically-modelled actors whose only motion is an OverrideLimbDraw-style rotation —
// En_Door's panel swing: the door has no CSAB, so the swing is a local-euler delta on the panel bone
// (set via Zelda3D_SetBoneRotDelta), exactly like N64 EnDoor rotating panel limb 4 by the open angle.
extern "C" void Zelda3D_UpdateBindPose(int modelId) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb) {
        return;
    }
    const float* drot = nullptr;
    int dcount = 0;
    const float* post = nullptr;
    int pcount = 0;
    getBoneRotDeltas(modelId, &drot, &dcount);
    getBonePostRots(modelId, &post, &pcount);
    std::vector<std::array<float, 16>> sm;
    Zelda3D::restPoseSkinMatrices(*lm->cmb, sm, drot, dcount, post, pcount);
    uploadSkin(modelId, lm, sm);
}

void Zelda3D_UpdateAnim(int modelId, const char* animName, float frame) {
    if (!animName || !*animName) {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb || !lm->zar)
        return;
    // Zelda3D #135 anim-selection probe: log once per (modelId, animName) pair so a
    // close-test (e.g. tools/entg_anim_close_test.py) can grep run.log for which CSAB
    // a particular model is actually being driven with. Emit-once per pair keeps the
    // log compact; the set is per-process so restarts reset it.
    {
        static std::set<std::pair<int, std::string>> sSeen;
        auto key = std::make_pair(modelId, std::string(animName));
        if (sSeen.insert(key).second) {
            fprintf(stderr, "[Zelda3D animPlay] model=%d anim='%s'\n", modelId, animName);
            fflush(stdout);
        }
    }

    Zelda3D::Csab* anim = getCsab(lm, animName);
    if (!anim) {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }

    std::vector<std::array<float, 16>> sm;
    const float* drot = nullptr;
    int dcount = 0;
    const float* post = nullptr;
    int pcount = 0;
    getBoneRotDeltas(modelId, &drot, &dcount);
    getBonePostRots(modelId, &post, &pcount);
    anim->skinMatrices(*lm->cmb, frame, sm, drot, dcount, post, pcount, getAnimTransScale(modelId));
    if (Zelda3D_SkinDumpActiveForModel(modelId)) {
        std::vector<std::array<float, 16>> aw;
        anim->animatedBoneWorld(*lm->cmb, frame, aw, drot, dcount, post, pcount, getAnimTransScale(modelId));
        Zelda3D_CaptureSkinDump(modelId, animName, frame, aw);
    }
    uploadSkin(modelId, lm, sm);
}

} // extern "C"

bool Zelda3D_ResolveAuthoredPoseInputs(int modelId, const char* animName, Zelda3D_AuthoredPoseInputs* outInputs) {
    if (!animName || !*animName || !outInputs)
        return false;
    *outInputs = {};
    outInputs->model = loadModel(modelId);
    if (!outInputs->model || !outInputs->model->ok || !outInputs->model->cmb || !outInputs->model->zar) {
        return false;
    }
    outInputs->animation = getCsab(outInputs->model, animName);
    if (!outInputs->animation)
        return false;
    getBoneRotDeltas(modelId, &outInputs->rotationDeltas, &outInputs->rotationDeltaCount);
    getBonePostRots(modelId, &outInputs->postRotations, &outInputs->postRotationCount);
    outInputs->rootMotion = getAnimTransScale(modelId);
    return true;
}

extern "C" {

extern "C" void Zelda3D_UpdateAnimWorldBones(int modelId, const char* animName, float frame, int firstBone,
                                             const float* worldMatrices3x4, int matrixCount) {
    if (!animName || !*animName || !worldMatrices3x4 || matrixCount <= 0 || firstBone < 0) {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }
    LoadedModel* lm = nullptr;
    std::vector<std::array<float, 16>> world;
    if (!Zelda3D_EvaluateAnimatedBoneWorld(modelId, animName, frame, &lm, world)) {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }
    const int endBone = std::min(firstBone + matrixCount, static_cast<int>(world.size()));
    for (int bone = firstBone; bone < endBone; ++bone) {
        const float* src = worldMatrices3x4 + (bone - firstBone) * 12;
        auto& dst = world[bone];
        dst = { src[0], src[1], src[2],  src[3],  src[4], src[5], src[6], src[7],
                src[8], src[9], src[10], src[11], 0.0f,   0.0f,   0.0f,   1.0f };
    }
    const auto& bind = lm->cmb->boneMatrices();
    std::vector<std::array<float, 16>> skin(bind.size(), Zelda3D::matId());
    for (size_t bone = 0; bone < bind.size() && bone < world.size(); ++bone) {
        skin[bone] = Zelda3D::matMul(world[bone], Zelda3D::matInverse(bind[bone]));
    }
    uploadSkin(modelId, lm, skin);
}

// MORPH variant of Zelda3D_UpdateAnim: cross-fade the INCOMING clip (inName@fIn) toward the frozen
// OUTGOING clip (outName@fOut) by `weight` (= N64 morphWeight, 1->0 over the transition). Same
// model, same upload tail. If the outgoing CSAB can't be resolved, falls back to a plain incoming
// sample (no morph) rather than dropping the pose.
static void Zelda3D_UpdateAnimMorph(int modelId, const char* inName, float fIn, const char* outName, float fOut,
                                    float weight) {
    if (!inName || !*inName) {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb || !lm->zar)
        return;
    Zelda3D::Csab* in = getCsab(lm, inName);
    if (!in) {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }
    Zelda3D::Csab* out = (outName && *outName) ? getCsab(lm, outName) : nullptr;
    const float* drot = nullptr;
    int dcount = 0;
    const float* post = nullptr;
    int pcount = 0;
    getBoneRotDeltas(modelId, &drot, &dcount);
    getBonePostRots(modelId, &post, &pcount);
    std::vector<std::array<float, 16>> sm;
    if (out) {
        in->skinMatricesMorph(*lm->cmb, fIn, *out, fOut, weight, sm, drot, dcount, post, pcount,
                              getAnimTransScale(modelId));
    } else {
        in->skinMatrices(*lm->cmb, fIn, sm, drot, dcount, post, pcount,
                         getAnimTransScale(modelId)); // outgoing unresolved -> no blend
    }
    if (Zelda3D_SkinDumpActiveForModel(modelId)) {
        std::vector<std::array<float, 16>> aw;
        if (out)
            in->animatedBoneWorldMorph(*lm->cmb, fIn, *out, fOut, weight, aw, drot, dcount, post, pcount,
                                       getAnimTransScale(modelId));
        else
            in->animatedBoneWorld(*lm->cmb, fIn, aw, drot, dcount, post, pcount, getAnimTransScale(modelId));
        Zelda3D_CaptureSkinDump(modelId, inName, fIn, aw);
    }
    uploadSkin(modelId, lm, sm);
}

extern "C" void Zelda3D_UpdateAnimAuthoredMorph(int modelId, const char* inName, float inFrame, const char* outName,
                                                float outFrame, float weight) {
    Zelda3D_UpdateAnimMorph(modelId, inName, inFrame, outName, outFrame, weight);
}

// TWO-SOURCE per-limb blend (#85 carry-WALK): drive the LOWER body from `lowerAnim` (the locomotion
// CSAB, free-run by `lowerRate` frames/draw exactly like the loco branch of Zelda3D_UpdateAnimAuto —
// the run/walk cycle's curFrame is dead during steady movement, so we advance by ground speed) and
// the UPPER body from `upperAnim` (the carry pose CSAB). For each bone, `upperMask[id] != 0` selects
// the upper clip (mask = the OoT3D analogue of sUpperBodyLimbCopyMap; see zelda3d_link.cpp). The upper
// clip is phase-locked to its N64 progress when meaningful (upperAnimLength > 4), else free-run, so a
// real carry hold (carryB_wait, near-static) plays faithfully without a leg cycle bleeding into it.
// This replaces the carry-walk N64-retarget detour in the 3DS-CSAB Link path (the last retarget dep).
void Zelda3D_UpdateAnimTwoSource(int modelId, const char* lowerAnim, float lowerRate, const char* upperAnim,
                                 float upperCurFrame, float upperAnimLength, const unsigned char* upperMask,
                                 int maskCount) {
    static std::unordered_map<int, float> lowerFrames;     // per-model loco free-run playhead
    static std::unordered_map<int, std::string> lastLower; // last lower CSAB (restart on change)
    static std::unordered_map<int, float> upperFrames;     // per-model upper free-run playhead (fallback)
    static std::unordered_map<int, std::string> lastUpper;
    if (!lowerAnim || !*lowerAnim || !upperAnim || !*upperAnim) {
        lowerFrames.erase(modelId);
        lastLower.erase(modelId);
        upperFrames.erase(modelId);
        lastUpper.erase(modelId);
        Zelda3D_UpdateAnim(modelId, nullptr, 0);
        return;
    }
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb || !lm->zar)
        return;
    Zelda3D::Csab* lower = getCsab(lm, lowerAnim);
    Zelda3D::Csab* upper = getCsab(lm, upperAnim);
    if (!lower || !upper) {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }

    // LOWER: free-run by ground speed (legs cycle), restart on a CSAB change so a fresh clip plays
    // from frame 0 rather than resuming the previous one's phase.
    {
        auto llIt = lastLower.find(modelId);
        if (llIt == lastLower.end() || llIt->second != lowerAnim)
            lowerFrames[modelId] = 0.0f;
        lastLower[modelId] = lowerAnim;
    }
    float fLower = lowerFrames[modelId];
    lowerFrames[modelId] += lowerRate;

    // UPPER: phase-lock to the N64 carry anim's progress when it carries real progress, else free-run
    // (matching Zelda3D_UpdateAnimAuto's lock/free choice).
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
        if (luIt == lastUpper.end() || luIt->second != upperAnim)
            upperFrames[modelId] = 0.0f;
        lastUpper[modelId] = upperAnim;
        fUpper = upperFrames[modelId];
        upperFrames[modelId] += gZelda3dAnimRate;
    }

    const float* drot = nullptr;
    int dcount = 0;
    const float* post = nullptr;
    int pcount = 0;
    getBoneRotDeltas(modelId, &drot, &dcount);
    getBonePostRots(modelId, &post, &pcount);
    std::vector<std::array<float, 16>> sm;
    lower->skinMatricesTwoSource(*lm->cmb, fLower, *upper, fUpper, upperMask, maskCount, sm, drot, dcount, post, pcount,
                                 getAnimTransScale(modelId));
    if (Zelda3D_SkinDumpActiveForModel(modelId)) {
        std::vector<std::array<float, 16>> aw;
        lower->animatedBoneWorldTwoSource(*lm->cmb, fLower, *upper, fUpper, upperMask, maskCount, aw, drot, dcount,
                                          post, pcount, getAnimTransScale(modelId));
        Zelda3D_CaptureSkinDump(modelId, lowerAnim, fLower, aw);
    }
    uploadSkin(modelId, lm, sm);
}

} // extern "C"
