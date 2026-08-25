// Narrow authored-pose evaluation interface for behavior-owned procedural placement.
#ifndef ZELDA3D_ANIM_POSE_EVALUATION_H
#define ZELDA3D_ANIM_POSE_EVALUATION_H

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_AnimWorldBone(int modelId, const char* animName, float frame, int boneIndex, float* outMatrix4x4);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_ANIM_POSE_EVALUATION_H
