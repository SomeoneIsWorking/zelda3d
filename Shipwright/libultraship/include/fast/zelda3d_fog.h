// Native renderer fog controls.
#ifndef ZELDA3D_FAST_FOG_H
#define ZELDA3D_FAST_FOG_H

#ifdef __cplusplus
extern "C" {
#endif

extern int gZelda3dFog3dForceOff;
extern int gZelda3dFogEnable;
extern int gZelda3dFogOverride;
extern float gZelda3dFogColor[3];
extern float gZelda3dFogMul;
extern float gZelda3dFogOffset;
extern int gZelda3dFog3dOn;
extern float gZelda3dFog3d[8];

void Zelda3D_Fog3dSet(float camNear, float zFar, float fogNear, float fogFar, const float eyeWorld[3],
                      const float fwdWorld[3]);
void Zelda3D_Fog3dOff(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_FAST_FOG_H
