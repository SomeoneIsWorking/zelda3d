#include "soh_environment_state.h"

#include "fast/zelda3d_lighting.h"
#include "functions/ui.h"
#include "global.h"
#include "z64light.h"

extern "C" {
int SohState_ShrinkWindowVal(void) {
    return static_cast<int>(ShrinkWindow_GetCurrentVal());
}

int SohState_Zelda3DLive(float* ambient, float* light1Color, float* light2Color) {
    for (int index = 0; index < 3; ++index) {
        ambient[index] = gZelda3dAmbient[index];
        light1Color[index] = gZelda3dLight1Col[index];
        light2Color[index] = gZelda3dLight2Col[index];
    }
    return 1;
}

int SohState_DayTimeAndEnv(unsigned int* daytime, unsigned char* skybox1Idx, unsigned char* skybox2Idx,
                           float* skyboxBlend, unsigned char* liveAmbient, unsigned char* liveFogColor,
                           short* liveFogNear, short* liveFogFar) {
    if (gPlayState == nullptr) {
        return 0;
    }
    *daytime = gSaveContext.dayTime;
    *skybox1Idx = gPlayState->envCtx.skybox1Index;
    *skybox2Idx = gPlayState->envCtx.skybox2Index;
    *skyboxBlend = gPlayState->envCtx.skyboxBlend;
    const LightContext& light = gPlayState->lightCtx;
    for (int index = 0; index < 3; ++index) {
        liveAmbient[index] = light.ambientColor[index];
        liveFogColor[index] = light.fogColor[index];
    }
    *liveFogNear = light.fogNear;
    *liveFogFar = light.fogFar;
    return 1;
}

int SohState_MoonDebug(float* sunPosY, float* color, float* scale, float* discScale) {
    if (gPlayState == nullptr) {
        return 0;
    }
    const float normalizedY = gPlayState->envCtx.sunPos.y / 25.0F;
    float intensity = -normalizedY / 120.0F;
    if (intensity < 0.0F) {
        intensity = 0.0F;
    }
    const float moonScale = (-15.0F * intensity) + 25.0F;
    *sunPosY = gPlayState->envCtx.sunPos.y;
    *color = intensity;
    *scale = moonScale;
    *discScale = moonScale * 0.505F;
    return 1;
}

int SohState_SetEnvSlot(unsigned char slot) {
    if (gPlayState == nullptr) {
        return 0;
    }
    gPlayState->envCtx.unk_BF = slot;
    gPlayState->envCtx.unk_D8 = 1.0F;
    return 1;
}

} // extern "C"
