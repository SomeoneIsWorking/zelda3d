// Shared OoT3D NPC procedural draw channels. See npc_draw.h.
#include "npc_draw.h"

#include "../../anim/zelda3d_anim_override.h"
#include "../../render/model_queries.h"
#include "asset/mat4.h"
#include "fast/zelda3d_material_overrides.h"

namespace Zelda3D {

namespace {
constexpr float kBinangToRad = 3.14159265358979f / 32768.0f;
}

void applyTrackRot(int modelId, int bone, const Vec3s& rot) {
    const Mat4 matrix = matMul(matRx(rot.y * kBinangToRad), matRz(rot.x * kBinangToRad));
    const float matrix3x3[9] = { matrix[0], matrix[1], matrix[2], matrix[4], matrix[5],
                                 matrix[6], matrix[8], matrix[9], matrix[10] };
    Zelda3D_SetBonePostRot(modelId, bone, matrix3x3);
}

void applyHeadTorsoTrack(int modelId, int headBone, int torsoBone, const NpcInteractInfo& interactInfo) {
    applyTrackRot(modelId, headBone, interactInfo.headRot);
    applyTrackRot(modelId, torsoBone, interactInfo.torsoRot);
}

void applyFacialFrame(int modelId, int material, int liveIndex) {
    if (material < 0) {
        return;
    }
    const int frame = gZelda3dFaceForce >= 0 ? gZelda3dFaceForce : liveIndex;
    const int texture = Zelda3D_FacialFrameTex(modelId, material, frame);
    Zelda3D_GL_SetMatTexOverride(modelId, material, texture);
}

} // namespace Zelda3D
