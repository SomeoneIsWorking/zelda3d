#include "soh_environment_state.h"

#include "fast/zelda3d_lighting.h"
#include "functions/ui.h"
#include "global.h"
#include "z64light.h"
// The 3DS scene-light palette feed (gZelda3dEnvColors) lives in the GAME's lighting owner, not in

extern "C" {
int SohState_ShrinkWindowVal(void) {
    return static_cast<int>(ShrinkWindow_GetCurrentVal());
}

int SohState_Zelda3DLive(float* ambient, float* light1Color, float* light2Color, float* n64Ambient) {
    // `gZelda3dAmbient` IS the submitted ambient: Zelda3D_GL_SetLightParams writes it, and
    // Zelda3D_UpdateSceneLighting -> Zelda3D_SubmitSceneLightColors is its only caller. At the title
    // that is the 3DS title-palette blend, because gZelda3dEnvColors.valid is 0 there and the
    // submission takes envCtx.lightSettings, which Zelda3D_Title_ApplyLightOverride fills from the
    // palette.
    //
    // It is deliberately NOT `gZelda3dWorldAmbColor`, which looks like the obvious choice (it is what
    // the shader reads) but is written only when `gZelda3dWorldAmbOverride` is 0 -- and that defaults
    // to 1, so it sits at its init value (0,0,1) forever. Reading it reports a pure-blue ambient that
    // the renderer never used, which is a false-divergence generator in the opposite direction.
    //
    // `n64Ambient` out-param carries gZelda3dWorldAmbColor so both are visible and neither can be
    // mistaken for the other. The name is historical; see the comment above.
    for (int index = 0; index < 3; ++index) {
        ambient[index] = gZelda3dAmbient[index];
        n64Ambient[index] = gZelda3dWorldAmbColor[index];
    }
    if (gPlayState != nullptr) {
        const EnvLightSettings& settings = gPlayState->envCtx.lightSettings;
        for (int index = 0; index < 3; ++index) {
            light1Color[index] = static_cast<float>(settings.light1Color[index]) / 255.0f;
            light2Color[index] = static_cast<float>(settings.light2Color[index]) / 255.0f;
        }
    } else {
        for (int index = 0; index < 3; ++index) {
            light1Color[index] = 0.0f;
            light2Color[index] = 0.0f;
        }
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
