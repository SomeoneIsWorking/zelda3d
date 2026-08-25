// Read-only access to live model-placement tuning for shipping render owners.
#ifndef ZELDA3D_DIAGNOSTICS_MODEL_TUNING_QUERY_H
#define ZELDA3D_DIAGNOSTICS_MODEL_TUNING_QUERY_H

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_ModelScaleOverride(int slot, float* outScale);
float Zelda3D_ModelScaleOrDefault(int slot, float fallback);
float Zelda3D_ModelAutoYOffsetNudge(void);
void Zelda3D_ModelRotationDegrees(float* outX, float* outY, float* outZ);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_DIAGNOSTICS_MODEL_TUNING_QUERY_H
