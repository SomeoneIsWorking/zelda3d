#pragma once

#ifdef __cplusplus
extern "C" {
#endif

float Zelda3D_MM_ModelBoneLenSum(int modelId);
float Zelda3D_MM_ModelMinY(int modelId);
void Zelda3D_MM_DumpModelBones(int modelId, int count);

#ifdef __cplusplus
}
#endif
