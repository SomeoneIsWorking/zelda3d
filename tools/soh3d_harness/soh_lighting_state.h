#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_LIGHTING_STATE_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_LIGHTING_STATE_H

extern "C" int SohState_Lighting(unsigned char ambient[3], signed char light1Dir[3], unsigned char light1Color[3],
                                 signed char light2Dir[3], unsigned char light2Color[3], unsigned char fogColor[3],
                                 short* fogNear, short* fogFar, unsigned char lightCtxAmbient[3],
                                 unsigned char lightCtxFogColor[3], short* lightCtxFogNear, short* lightCtxFogFar,
                                 unsigned char* outUnkBF, unsigned char* outUnkBD, float* outUnkD8);

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_LIGHTING_STATE_H
