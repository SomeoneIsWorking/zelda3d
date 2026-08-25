#include "soh_lighting_state.h"

#include "global.h"
#include "z64environment.h"
#include "z64light.h"

extern "C" int SohState_Lighting(unsigned char ambient[3], signed char light1Dir[3], unsigned char light1Color[3],
                                 signed char light2Dir[3], unsigned char light2Color[3], unsigned char fogColor[3],
                                 short* fogNear, short* fogFar, unsigned char lightCtxAmbient[3],
                                 unsigned char lightCtxFogColor[3], short* lightCtxFogNear, short* lightCtxFogFar,
                                 unsigned char* outUnkBF, unsigned char* outUnkBD, float* outUnkD8) {
    if (gPlayState == nullptr) {
        return 0;
    }
    const EnvLightSettings& settings = gPlayState->envCtx.lightSettings;
    if (outUnkBF != nullptr) {
        *outUnkBF = gPlayState->envCtx.unk_BF;
    }
    if (outUnkBD != nullptr) {
        *outUnkBD = gPlayState->envCtx.unk_BD;
    }
    if (outUnkD8 != nullptr) {
        *outUnkD8 = gPlayState->envCtx.unk_D8;
    }
    for (int index = 0; index < 3; ++index) {
        ambient[index] = settings.ambientColor[index];
        light1Dir[index] = settings.light1Dir[index];
        light1Color[index] = settings.light1Color[index];
        light2Dir[index] = settings.light2Dir[index];
        light2Color[index] = settings.light2Color[index];
        fogColor[index] = settings.fogColor[index];
    }
    *fogNear = settings.fogNear;
    *fogFar = settings.fogFar;
    const LightContext& light = gPlayState->lightCtx;
    for (int index = 0; index < 3; ++index) {
        lightCtxAmbient[index] = light.ambientColor[index];
        lightCtxFogColor[index] = light.fogColor[index];
    }
    *lightCtxFogNear = light.fogNear;
    *lightCtxFogFar = light.fogFar;
    return 1;
}
