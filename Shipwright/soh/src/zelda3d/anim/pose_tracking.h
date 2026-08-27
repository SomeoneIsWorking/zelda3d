// Posed-geometry tracking and measurement interface.
#ifndef ZELDA3D_ANIM_POSE_TRACKING_H
#define ZELDA3D_ANIM_POSE_TRACKING_H

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_DumpBoneStats(int modelId);
void Zelda3D_SetTrackPosedMinY(int modelId, int enable);
float Zelda3D_PosedGroundOffset(int modelId, unsigned long long midMask);
int Zelda3D_PosedModelLocalAABB(int modelId, unsigned long long midMask, float* outMin, float* outMax);
int Zelda3D_PosedBonePoint(int modelId, int boneId, const float* boneLocalPoint, float* outModelPos);
int Zelda3D_PosedBoneWorldPos(int modelId, int boneId, float* outModelPos);
void Zelda3D_AnimResetRunState(void);
float Zelda3D_PoseDiscontinuity(int modelId, int* outBone);
void Zelda3D_PoseScanReset(int modelId);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_ANIM_POSE_TRACKING_H
