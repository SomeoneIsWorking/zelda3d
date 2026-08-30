// Scene lighting inputs and direct per-model lighting overrides.
#ifndef ZELDA3D_FAST_LIGHTING_H
#define ZELDA3D_FAST_LIGHTING_H

#ifdef __cplusplus
extern "C" {
#endif

// Direction TO the light, in the F3DEX/OoT convention. The shader normalizes it.
void Zelda3D_GL_SetLightDir(const float dirWorld[3]);
void Zelda3D_GL_SetLightParams(const float ambient[3], const float light1Color[3], const float light2Direction[3],
                               const float light2Color[3], int enabledLightCount);
void Zelda3D_GL_SetLightDirOverride(int modelId, float dx, float dy, float dz);
void Zelda3D_GL_ClearLightDirOverride(int modelId);
void Zelda3D_GL_SetSphereMapNormalMatrix(int modelId, const float matrix[9]);
void Zelda3D_GL_ClearSphereMapNormalMatrix(int modelId);
extern float gZelda3dAmbient[3];
extern float gZelda3dLight1Col[3];
extern float gZelda3dLight2Dir[3];
extern float gZelda3dLight2Col[3];
extern float gZelda3dLightDirWorld[3];
extern float gZelda3dAmbientLightCount;
extern float gZelda3dWorldAmb;
extern float gZelda3dWorldAmbColor[3];
extern int gZelda3dWorldAmbOverride;

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_FAST_LIGHTING_H
