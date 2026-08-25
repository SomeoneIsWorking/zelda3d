// Per-model skeleton pose upload and per-draw pose capture.
#ifndef ZELDA3D_FAST_POSE_H
#define ZELDA3D_FAST_POSE_H

#include "zelda3d_model_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_GL_EmitPose(int modelId);
void Zelda3D_GL_SetBones(int modelId, const float* mats16, int count);
void Zelda3D_GL_SetBoneBind(int modelId, const float* mats16, int count);
extern float gZelda3dInterpStep;

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_FAST_POSE_H
