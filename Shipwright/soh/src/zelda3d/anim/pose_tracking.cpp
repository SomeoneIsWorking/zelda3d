// Posed-geometry tracking and measurements derived from shipping skin matrices.
#include "pose_tracking_internal.h"

#include "../model/zelda3d_model_internal.h"
#include "asset/cmb.h"

#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

extern "C" void Zelda3D_DumpBoneStats(int modelId) {
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
                if (v.weights[k] <= 0.0f) {
                    continue;
                }
                int b = (int)(v.boneIds[k] + 0.5f);
                if (b < 0 || b >= n) {
                    continue;
                }
                vc[b]++;
                mx[b] += v.pos[0];
                my[b] += v.pos[1];
                mz[b] += v.pos[2];
            }
        }
    }
    fprintf(stderr, "[BONESTATS] model %d bones=%d\n", modelId, n);
    for (const auto& bn : bones) {
        int id = bn.id;
        if (id < 0 || id >= n) {
            continue;
        }
        int c = vc[id];
        fprintf(stderr, "[BONESTATS]  bone %2d parent %2d verts %5d meanPos(%.1f,%.1f,%.1f) trans(%.1f,%.1f,%.1f)\n",
                id, bn.parent, c, c ? mx[id] / c : 0.0, c ? my[id] / c : 0.0, c ? mz[id] / c : 0.0, bn.trans[0],
                bn.trans[1], bn.trans[2]);
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
// Cache this frame's skin matrices for a tracked model so Zelda3D_PosedGroundOffset can recompute the
// posed feet position against the (later-known) mesh_id mask. No-op unless tracking is enabled.
void Zelda3D_CacheTrackedPose(int modelId, const std::vector<std::array<float, 16>>& sm) {
    auto it = trackMinYFlags().find(modelId);
    if (it == trackMinYFlags().end() || !it->second) {
        return;
    }
    lastSkin()[modelId] = sm;
}
extern "C" void Zelda3D_SetTrackPosedMinY(int modelId, int enable) {
    trackMinYFlags()[modelId] = enable ? 1 : 0;
    if (!enable) {
        lastSkin().erase(modelId);
    }
}
// Model-local Y translation to add (innermost, pre-scale) so the posed model's lowest VISIBLE
// vertex lands on the actor's ground. midMask selects the drawn equipment/hand variant subset
// (same bit convention as Zelda3D_GL_SetMidMask: bit i = mesh_id i visible; mesh_id<0 or >=64 always
// shown). Returns 0 if tracking wasn't enabled / no pose cached.
extern "C" float Zelda3D_PosedGroundOffset(int modelId, unsigned long long midMask) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) {
        return 0.0f;
    }
    auto it = lastSkin().find(modelId);
    if (it == lastSkin().end() || it->second.empty()) {
        return 0.0f;
    }
    const auto& sm = it->second;
    const int n = (int)sm.size();
    float mn = 1e30f;
    for (const auto& g : lm->groups) {
        if (g.mesh_id >= 0 && g.mesh_id < 64 && !((midMask >> g.mesh_id) & 1ull)) {
            continue;
        }
        for (const auto& v : g.verts) {
            float y = 0.0f, wsum = 0.0f;
            for (int k = 0; k < 4; k++) {
                float w = v.weights[k];
                if (w <= 0.0f) {
                    continue;
                }
                int b = (int)(v.boneIds[k] + 0.5f);
                if (b < 0 || b >= n) {
                    continue;
                }
                const float* M = sm[b].data();
                y += w * (M[4] * v.pos[0] + M[5] * v.pos[1] + M[6] * v.pos[2] + M[7]);
                wsum += w;
            }
            if (wsum > 0.0f) {
                y /= wsum;
                if (y < mn) {
                    mn = y;
                }
            }
        }
    }
    return (mn < 1e29f) ? -mn : 0.0f;
}

// Headless-observation tooling: the POSED model-local AABB (min/max over the visible, skinned
// vertices) of `modelId` this frame. Lets the REPL frame the camera on where an actor's model
// ACTUALLY draws — not its world.pos anchor — which for posed/offset actors (Queen Gohma on the
// ceiling, flying creatures, held items) can be far from the anchor. Same skin transform + midMask
// convention as Zelda3D_PosedGroundOffset. Falls back to the BIND/static vertex AABB when no posed
// pose is cached (un-skinned props, or before tracking warms up), so it always yields a usable box.
// Returns 1 on success (outMin/outMax = 3 floats, model-local space), 0 if the model isn't loaded.
extern "C" int Zelda3D_PosedModelLocalAABB(int modelId, unsigned long long midMask, float* outMin, float* outMax) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !outMin || !outMax) {
        return 0;
    }
    auto it = lastSkin().find(modelId);
    const std::vector<std::array<float, 16>>* sm =
        (it != lastSkin().end() && !it->second.empty()) ? &it->second : nullptr;
    const int n = sm ? (int)sm->size() : 0;
    float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
    bool any = false;
    for (const auto& g : lm->groups) {
        if (g.mesh_id >= 0 && g.mesh_id < 64 && !((midMask >> g.mesh_id) & 1ull)) {
            continue;
        }
        for (const auto& v : g.verts) {
            float p[3];
            if (sm) {
                // skinned: weighted sum of bone transforms (rows 0/1/2 of each 4x4, row-major).
                float acc[3] = { 0, 0, 0 }, wsum = 0.0f;
                for (int k = 0; k < 4; k++) {
                    float w = v.weights[k];
                    if (w <= 0.0f) {
                        continue;
                    }
                    int b = (int)(v.boneIds[k] + 0.5f);
                    if (b < 0 || b >= n) {
                        continue;
                    }
                    const float* M = (*sm)[b].data();
                    acc[0] += w * (M[0] * v.pos[0] + M[1] * v.pos[1] + M[2] * v.pos[2] + M[3]);
                    acc[1] += w * (M[4] * v.pos[0] + M[5] * v.pos[1] + M[6] * v.pos[2] + M[7]);
                    acc[2] += w * (M[8] * v.pos[0] + M[9] * v.pos[1] + M[10] * v.pos[2] + M[11]);
                    wsum += w;
                }
                if (wsum <= 0.0f) {
                    p[0] = v.pos[0];
                    p[1] = v.pos[1];
                    p[2] = v.pos[2];
                } else {
                    p[0] = acc[0] / wsum;
                    p[1] = acc[1] / wsum;
                    p[2] = acc[2] / wsum;
                }
            } else {
                p[0] = v.pos[0];
                p[1] = v.pos[1];
                p[2] = v.pos[2]; // bind/static fallback
            }
            for (int a = 0; a < 3; a++) {
                if (p[a] < mn[a]) {
                    mn[a] = p[a];
                }
                if (p[a] > mx[a]) {
                    mx[a] = p[a];
                }
            }
            any = true;
        }
    }
    if (!any) {
        return 0;
    }
    for (int a = 0; a < 3; a++) {
        outMin[a] = mn[a];
        outMax[a] = mx[a];
    }
    return 1;
}

// Model-local position of a posed bone's ORIGIN this frame, recovered from the cached skin matrices
// (#6 held-actor attach). The animated bone-world matrix is aw[b] = skin[b]*bind[b], so the bone
// origin in model space is skin[b] applied to the bind-pose origin (bind[b]'s translation column).
// Returns 1 and writes outModelPos (3 floats) on success; 0 if no pose is cached / bone out of range.
// The caller lifts this through the actor world matrix (Matrix_MultVec3f) to get world space. Uses
// the SAME lastSkin cache the feet-grounding path already maintains, so any posing path (N64-retarget
// or CSAB) that ran Zelda3D_CacheTrackedPose this frame exposes it. Requires Zelda3D_SetTrackPosedMinY(1).
extern "C" int Zelda3D_PosedBonePoint(int modelId, int boneId, const float* boneLocalPoint, float* outModelPos) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb || !boneLocalPoint || !outModelPos) {
        return 0;
    }
    auto it = lastSkin().find(modelId);
    if (it == lastSkin().end() || it->second.empty()) {
        return 0;
    }
    const auto& sm = it->second;
    const auto& bind = lm->cmb->boneMatrices();
    if (boneId < 0 || (size_t)boneId >= sm.size() || (size_t)boneId >= bind.size()) {
        return 0;
    }
    const float* M = sm[boneId].data();
    const float* B = bind[boneId].data();
    // Convert the bone-local point through its bind-world transform, then through the cached skin
    // matrix (animatedWorld * inverse(bindWorld)). The product is the exact animated model-space
    // point used by the CMB draw.
    const float bx = B[0] * boneLocalPoint[0] + B[1] * boneLocalPoint[1] + B[2] * boneLocalPoint[2] + B[3];
    const float by = B[4] * boneLocalPoint[0] + B[5] * boneLocalPoint[1] + B[6] * boneLocalPoint[2] + B[7];
    const float bz = B[8] * boneLocalPoint[0] + B[9] * boneLocalPoint[1] + B[10] * boneLocalPoint[2] + B[11];
    outModelPos[0] = M[0] * bx + M[1] * by + M[2] * bz + M[3];
    outModelPos[1] = M[4] * bx + M[5] * by + M[6] * bz + M[7];
    outModelPos[2] = M[8] * bx + M[9] * by + M[10] * bz + M[11];
    return 1;
}

extern "C" int Zelda3D_PosedBoneWorldPos(int modelId, int boneId, float* outModelPos) {
    const float origin[3] = { 0.0f, 0.0f, 0.0f };
    return Zelda3D_PosedBonePoint(modelId, boneId, origin, outModelPos);
}

// Pose-discontinuity scanner (anim QA tooling): the 3d3 named-CSAB path picks ONE csab at a phase and
// never blends morphs, so any transition that hard-cuts the pose shows as a per-bone rotation that JUMPS
// between consecutive frames far beyond what a continuous animation could produce. This compares the
// current cached pose (lastSkin) against the previous snapshot and returns the LARGEST per-bone rotation
// delta (degrees) plus that bone. Generic: works for ANY animation/transition driven through the model,
// needs no oracle. Orthonormalizes each bone's 3x3 (removing skin scale) then measures the relative
// rotation angle acos((tr(Ra^T Rb)-1)/2). First call after a reset returns 0 (no previous). Pair with
// the freeze/step harness + the action machine to sweep transitions and auto-flag pops. Uses lastSkin,
// so it requires Zelda3D_SetTrackPosedMinY(1) on the model (the player path already enables it).
static std::unordered_map<int, std::vector<std::array<float, 16>>>& posePrev() {
    static std::unordered_map<int, std::vector<std::array<float, 16>>> m;
    return m;
}
// Run-scoped reset for the zelda3d anim layer, called from Zelda3D_CoreRunBegin.
//
// THE DISTINCTION THIS ENCODES, because the audit of docs/issues/0016 could not settle it by reading
// and it decides ~40 caches either way: the zelda3d caches keyed by modelId split into two kinds,
// and only one of them belongs to a run.
//
//   ASSET-derived, and correctly ENGINE-scoped: boneRotDeltas, bonePostRots, rootMotions,
//   trackMinYFlags, the model/atlas caches. They hold OWNED data (vectors, not pointers into a
//   ResourceManager), and Zelda3D_AutoModelId maps a ZAR PATH to an id through a process-lifetime
//   table -- so the same asset gets the same id in every run and a cache hit in run 2 is a hit on
//   exactly the same asset. Nothing dangles and nothing collides. Keeping them across runs is not a
//   leak being tolerated; it is the cache working.
//
//   PER-FRAME POSE, and run-scoped: lastSkin and posePrev are last frame's skin matrices. Because
//   the id space IS stable, run 2 would find run 1's final pose under the same key and blend its
//   first frame from a pose belonging to a game that has ended. Not a crash -- one frame of wrong
//   interpolation, which is precisely the kind of thing nobody would trace back here.
extern "C" void Zelda3D_AnimResetRunState(void) {
    lastSkin().clear();
    posePrev().clear();
}

static void orthoRows(const float* M, float r[3][3]) {
    // row-major 4x4 rotation rows (v' = M*v): r0..r2; Gram-Schmidt to a pure rotation.
    float a[3] = { M[0], M[1], M[2] }, b[3] = { M[4], M[5], M[6] }, c[3] = { M[8], M[9], M[10] };
    auto norm = [](float* v) {
        float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (n > 1e-8f) {
            v[0] /= n;
            v[1] /= n;
            v[2] /= n;
        }
    };
    auto dot = [](const float* u, const float* v) { return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]; };
    norm(a);
    float pb = dot(a, b);
    b[0] -= pb * a[0];
    b[1] -= pb * a[1];
    b[2] -= pb * a[2];
    norm(b);
    c[0] = a[1] * b[2] - a[2] * b[1];
    c[1] = a[2] * b[0] - a[0] * b[2];
    c[2] = a[0] * b[1] - a[1] * b[0]; // c = a x b
    for (int k = 0; k < 3; k++) {
        r[0][k] = a[k];
        r[1][k] = b[k];
        r[2][k] = c[k];
    }
}
extern "C" float Zelda3D_PoseDiscontinuity(int modelId, int* outBone) {
    if (outBone) {
        *outBone = -1;
    }
    auto it = lastSkin().find(modelId);
    if (it == lastSkin().end() || it->second.empty()) {
        return 0.0f;
    }
    const auto& cur = it->second;
    auto& prev = posePrev()[modelId];
    float maxDeg = 0.0f;
    int maxBone = -1;
    if (prev.size() == cur.size()) {
        for (size_t b = 0; b < cur.size(); b++) {
            float Ra[3][3], Rb[3][3];
            orthoRows(prev[b].data(), Ra);
            orthoRows(cur[b].data(), Rb);
            // tr(Ra^T * Rb) = sum_ij Ra[i][j]*Rb[i][j]  (Ra rows are basis vectors)
            float tr = 0.0f;
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    tr += Ra[i][j] * Rb[i][j];
                }
            }
            float cosA = (tr - 1.0f) * 0.5f;
            if (cosA > 1.0f) {
                cosA = 1.0f;
            }
            if (cosA < -1.0f) {
                cosA = -1.0f;
            }
            float deg = std::acos(cosA) * (180.0f / 3.14159265358979f);
            if (deg > maxDeg) {
                maxDeg = deg;
                maxBone = (int)b;
            }
        }
    }
    prev = cur; // snapshot for next call
    if (outBone) {
        *outBone = maxBone;
    }
    return maxDeg;
}
extern "C" void Zelda3D_PoseScanReset(int modelId) {
    posePrev().erase(modelId);
}
