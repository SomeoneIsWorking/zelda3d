// Animation-pose diagnostics and resolved-pose capture.
#ifndef ZELDA3D_ANIM_POSE_INSPECTION_H
#define ZELDA3D_ANIM_POSE_INSPECTION_H

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_SkinDumpArm(int modelId, const char* path, int frames);
int Zelda3D_GetAnimBonesLocal(int modelId, const char* animName, float frame, float* outRot3, int* outId,
                              int* outParent, int maxBones, char* outCsab, int outCsabLen, float* outResolvedFrame);
void Zelda3D_DumpAnimBonesLocal(int modelId, const char* animName, float frame);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_ANIM_POSE_INSPECTION_H
