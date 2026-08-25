#include "title_lighting.h"

#include "title_activity.h"
#include "../../cutscene/zelda3d_cutscene.h"
#include "fast/zelda3d_fog.h"

#include <cmath>

namespace {

bool ApplyPalette(PlayState* play, uint16_t dayTime) {
    uint8_t ambient[3];
    uint8_t light1Color[3];
    uint8_t light2Color[3];
    uint8_t fogColor[3];
    int8_t ignoredLight1Direction[3];
    int8_t ignoredLight2Direction[3];
    if (!Zelda3D_TitleCsBlendedLight(dayTime, ambient, ignoredLight1Direction, light1Color, ignoredLight2Direction,
                                     light2Color, fogColor)) {
        return false;
    }

    for (int i = 0; i < 3; ++i) {
        play->envCtx.lightSettings.ambientColor[i] = ambient[i];
        play->envCtx.lightSettings.light1Color[i] = light1Color[i];
        play->envCtx.lightSettings.light2Color[i] = light2Color[i];
        play->envCtx.lightSettings.fogColor[i] = fogColor[i];
    }

    // OoT3D derives the sun direction from day time instead of reading the palette directions.
    const float radians = static_cast<float>(dayTime) * (3.14159265f * 2.0f / 65536.0f);
    const float direction[3] = {
        -120.0f * std::sin(radians),
        120.0f * std::cos(radians),
        20.0f * std::cos(radians),
    };
    for (int i = 0; i < 3; ++i) {
        const float component = direction[i];
        play->envCtx.lightSettings.light1Dir[i] =
            static_cast<int8_t>(component >= 0.0f ? component + 0.5f : component - 0.5f);
        play->envCtx.lightSettings.light2Dir[i] = -play->envCtx.lightSettings.light1Dir[i];
    }
    return true;
}

void ApplyDistanceFog(uint16_t dayTime) {
    float fogNear;
    float fogFar;
    float projectionFar;
    if (!Zelda3D_TitleCsBlendedFog(dayTime, &fogNear, &fogFar, &projectionFar)) {
        return;
    }

    float eye[3];
    float at[3];
    float up[3];
    float fov;
    const int frame = Zelda3D_TitleCsFrame();
    const float fractionalFrame = static_cast<float>(frame) + Zelda3D_TitleCsSubframe();
    int cameraLive = Zelda3D_TitleCsCamera(fractionalFrame, eye, at, up, &fov);
    if (!cameraLive && frame > 0) {
        cameraLive = Zelda3D_TitleCsCamera(static_cast<float>(frame - 1), eye, at, up, &fov);
    }
    if (!cameraLive) {
        return;
    }

    constexpr float kTitleCameraNear = 7.0f;
    const float forward[3] = { at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] };
    Zelda3D_Fog3dSet(kTitleCameraNear, projectionFar, fogNear, fogFar, eye, forward);
}

} // namespace

namespace Zelda3D {

void ClearTitleFog() {
    Zelda3D_Fog3dOff();
}

} // namespace Zelda3D

extern "C" void Zelda3D_Title_ApplyLightOverride(PlayState* play) {
    if (!Zelda3D_Title_IsActive() || play == nullptr) {
        return;
    }

    uint16_t dayTime;
    if (!Zelda3D_TitleCsTimeOfDay(static_cast<float>(Zelda3D_TitleCsFrame()) + Zelda3D_TitleCsSubframe(), &dayTime)) {
        return;
    }
    if (!ApplyPalette(play, dayTime)) {
        return;
    }
    ApplyDistanceFog(dayTime);
}
