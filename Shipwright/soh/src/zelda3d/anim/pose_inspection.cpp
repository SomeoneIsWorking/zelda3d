// Resolved-pose capture and local-transform diagnostics.
#include "pose_inspection_internal.h"

#include "automatic_playback.h"
#include "pose_evaluation_internal.h"

#include "../model/zelda3d_model_internal.h"
#include "asset/csab.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
extern "C" void Zelda3D_SkinDumpArm(int modelId, const char* path, int frames) {
    if (gSkinDumpFile) {
        fclose(gSkinDumpFile);
        gSkinDumpFile = nullptr;
    }
    gSkinDumpFile = fopen(path, "w");
    if (!gSkinDumpFile)
        return;
    fprintf(gSkinDumpFile, "# resolved CSAB pose capture: animated bone-WORLD matrix aw=skin*bind,\n");
    fprintf(gSkinDumpFile, "# row-major top 3 rows; bone world pos=(m3,m7,m11). Parity vs oracle link_skel_live.\n");
    fprintf(gSkinDumpFile, "cap,anim,frame,bone,m0,m1,m2,m3,m4,m5,m6,m7,m8,m9,m10,m11\n");
    gSkinDumpModel = modelId;
    gSkinDumpRemaining = frames;
    gSkinDumpCap = 0;
}
// `aw` is the animated bone-WORLD matrix straight from Csab::animatedBoneWorld (recursive parent
// multiply) — NOT reconstructed via skin*bind, which is lossy when bind carries scale (matInverse
// is approximate there). Bone world position = (m3,m7,m11).
bool Zelda3D_SkinDumpActiveForModel(int modelId) {
    return gSkinDumpFile && modelId == gSkinDumpModel && gSkinDumpRemaining > 0;
}

void Zelda3D_CaptureSkinDump(int modelId, const char* animName, float frame,
                             const std::vector<std::array<float, 16>>& aw) {
    if (!gSkinDumpFile || modelId != gSkinDumpModel || gSkinDumpRemaining <= 0)
        return;
    for (int b = 0; b < (int)aw.size(); b++) {
        const float* M = aw[b].data();
        fprintf(gSkinDumpFile, "%d,%s,%.3f,%d,%.4f,%.4f,%.4f,%.2f,%.4f,%.4f,%.4f,%.2f,%.4f,%.4f,%.4f,%.2f\n",
                gSkinDumpCap, animName ? animName : "(null)", frame, b, M[0], M[1], M[2], M[3], M[4], M[5], M[6], M[7],
                M[8], M[9], M[10], M[11]);
    }
    gSkinDumpCap++;
    if (--gSkinDumpRemaining == 0) {
        fclose(gSkinDumpFile);
        gSkinDumpFile = nullptr;
        gSkinDumpModel = -1;
    }
}

// CORE: fill per-bone ANIMATED LOCAL rotation (radians, rX/rY/rZ) at the given clip+frame, using
// the SAME sampling the renderer uses (Csab::localTransforms). With animName NULL, uses the clip+
// frame the live AUTO draw path last resolved for this model — so a caller gets the
// exact pose currently on screen. Returns bone count (>=0), or -1 on error. outRot3 holds
// count*3 floats (rX,rY,rZ per bone in CMB bone order); outParent[i]/outId[i] optional (may be
// NULL). Shared by the REPL `boneinfo` stderr dump and the harness `soh_titlebones` comparison.
extern "C" int Zelda3D_GetAnimBonesLocal(int modelId, const char* animName, float frame, float* outRot3, int* outId,
                                         int* outParent, int maxBones, char* outCsab, int outCsabLen,
                                         float* outResolvedFrame) {
    std::string csabName;
    float useFrame = frame;
    if (animName && *animName) {
        csabName = animName;
    } else {
        const char* lastCsab = nullptr;
        float lastFrame = 0.0f;
        if (!Zelda3D_LastAutoAnim(modelId, &lastCsab, &lastFrame) || !lastCsab)
            return -1;
        csabName = lastCsab;
        if (frame < 0.0f)
            useFrame = lastFrame;
    }
    Zelda3D_AuthoredPoseInputs inputs;
    if (!Zelda3D_ResolveAuthoredPoseInputs(modelId, csabName.c_str(), &inputs))
        return -1;
    std::vector<Zelda3D::Csab::BoneLocal> bl;
    inputs.animation->localTransforms(*inputs.model->cmb, useFrame, bl);
    int n = (int)bl.size();
    if (n > maxBones)
        n = maxBones;
    for (int i = 0; i < n; i++) {
        if (outRot3) {
            outRot3[i * 3 + 0] = bl[i].r[0];
            outRot3[i * 3 + 1] = bl[i].r[1];
            outRot3[i * 3 + 2] = bl[i].r[2];
        }
        if (outId)
            outId[i] = bl[i].id;
        if (outParent)
            outParent[i] = bl[i].parent;
    }
    if (outCsab && outCsabLen > 0) {
        strncpy(outCsab, csabName.c_str(), outCsabLen - 1);
        outCsab[outCsabLen - 1] = '\0';
    }
    if (outResolvedFrame)
        *outResolvedFrame = useFrame;
    return n;
}

// REPL `boneinfo <modelId> [anim] [frame]`: print the AUTO model's per-bone ANIMATED LOCAL rotation
// for a bone-for-bone quantitative diff against the OoT3D oracle's own live limb table (harness
// `titleactors a`). With anim/frame omitted, uses the values the live draw path last resolved.
extern "C" void Zelda3D_DumpAnimBonesLocal(int modelId, const char* animName, float frame) {
    float rot[25 * 3];
    int id[25], parent[25];
    char csab[64];
    float rf = 0.0f;
    int n = Zelda3D_GetAnimBonesLocal(modelId, animName, frame, rot, id, parent, 25, csab, sizeof csab, &rf);
    if (n < 0) {
        fprintf(stderr, "[BONEINFO] model %d: no cmb / csab not found / no live anim recorded\n", modelId);
        return;
    }
    fprintf(stderr, "[BONEINFO] model %d csab=%s frame=%.3f bones=%d\n", modelId, csab, rf, n);
    for (int i = 0; i < n; i++) {
        fprintf(stderr, "[BONEINFO] b id=%d parent=%d localRot=(%.4f,%.4f,%.4f)\n", id[i], parent[i], rot[i * 3 + 0],
                rot[i * 3 + 1], rot[i * 3 + 2]);
    }
    fflush(stderr);
}
