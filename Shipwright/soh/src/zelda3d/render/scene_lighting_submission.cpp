#include "scene_lighting_submission.h"

#include "../behaviors/title/title_activity.h"
#include "../cutscene/zelda3d_cutscene.h"
#include "../lighting/zelda3d_lighting.h"
#include "title_light_slots.h"

#include <fast/zelda3d_lighting.h>

#include "../tables/zelda3d_scene_lighting.inc"

#include <cmath>
#include <cstdint>

// The scene submission owns the last live world-space key direction and its REPL override. OoT
// folds the camera into the projection matrix, so the renderer consumes these world-space values
// directly; the gameplay/title convention conversion happens at the submission seam below.
int gZelda3dLightDirOverride = 0;
float gZelda3dLightDirLast[3] = { 0.40f, 0.55f, 0.73f };

static void Zelda3D_SelectScenePalette(PlayState* play) {
    const s32 sceneNumber = play->sceneNum;
    if (Zelda3D_Title_IsActive() && Zelda3D_TitleLightSlotCount() > 0) {
        // The title demo runs on spot99, whose light settings do not have an N64 scene number.
        gZelda3dScenePalette = Zelda3D_TitleLightSlots();
        gZelda3dScenePaletteN = Zelda3D_TitleLightSlotCount();
    } else if (sceneNumber >= 0 && sceneNumber < static_cast<s32>(ARRAY_COUNT(kZelda3dSceneLighting)) &&
               kZelda3dSceneLighting[sceneNumber].numSlots) {
        gZelda3dScenePalette = kZelda3dSceneLighting[sceneNumber].slots;
        gZelda3dScenePaletteN = kZelda3dSceneLighting[sceneNumber].numSlots;
    } else {
        gZelda3dScenePalette = nullptr;
        gZelda3dScenePaletteN = 0;
    }

    // Never apply the previous scene's captured blend indices to a newly selected palette.
    static const Zelda3dLightSlot* previousPalette = nullptr;
    if (gZelda3dScenePalette != previousPalette) {
        gZelda3dEnvBlend.valid = 0;
        previousPalette = gZelda3dScenePalette;
    }
}

static bool Zelda3D_TitleLightDirections(int8_t light1Direction[3], int8_t light2Direction[3]) {
    if (!Zelda3D_Title_IsActive()) {
        return false;
    }

    uint16_t timeOfDay;
    uint8_t ambient[3];
    uint8_t light1Color[3];
    uint8_t light2Color[3];
    uint8_t fogColor[3];
    return Zelda3D_TitleCsTimeOfDay(static_cast<float>(Zelda3D_TitleCsFrame()) + Zelda3D_TitleCsSubframe(),
                                    &timeOfDay) &&
           Zelda3D_TitleCsBlendedLight(timeOfDay, ambient, light1Direction, light1Color, light2Direction, light2Color,
                                       fogColor);
}

static void Zelda3D_SubmitSceneLightColors(const EnvLightSettings* settings, bool titleDirections,
                                           const int8_t titleLight2Direction[3]) {
    float ambient[3];
    float light1Color[3];
    float light2Direction[3];
    float light2Color[3];
    for (s32 component = 0; component < 3; ++component) {
        if (gZelda3dEnvColors.valid) {
            ambient[component] = gZelda3dEnvColors.amb[component];
            light1Color[component] = gZelda3dEnvColors.l1col[component];
            light2Color[component] = gZelda3dEnvColors.l2col[component];
        } else {
            ambient[component] = static_cast<float>(settings->ambientColor[component]) / 255.0f;
            light1Color[component] = static_cast<float>(settings->light1Color[component]) / 255.0f;
            light2Color[component] = static_cast<float>(settings->light2Color[component]) / 255.0f;
        }
        // Gameplay EnvLightSettings uses N64 toward-light directions, while the 3DS shader consumes
        // light-travel directions. Title slots are already authored in the 3DS convention.
        light2Direction[component] = titleDirections ? static_cast<float>(titleLight2Direction[component])
                                                     : -static_cast<float>(settings->light2Dir[component]);
    }

    const float length = std::sqrt(light2Direction[0] * light2Direction[0] + light2Direction[1] * light2Direction[1] +
                                   light2Direction[2] * light2Direction[2]);
    if (length > 0.5f) {
        light2Direction[0] /= length;
        light2Direction[1] /= length;
        light2Direction[2] /= length;
    }

    // Both EnvLightSettings slots are always bound. A zero direction suppresses only the diffuse
    // term; it does not disable that slot's ambient contribution.
    Zelda3D_GL_SetLightParams(ambient, light1Color, light2Direction, light2Color, 2);
    if (!gZelda3dWorldAmbOverride) {
        gZelda3dWorldAmbColor[0] = ambient[0];
        gZelda3dWorldAmbColor[1] = ambient[1];
        gZelda3dWorldAmbColor[2] = ambient[2];
    }
}

static void Zelda3D_SubmitPrimaryLightDirection(const EnvLightSettings* settings, bool titleDirections,
                                                const int8_t titleLight1Direction[3]) {
    if (gZelda3dLightDirOverride) {
        return;
    }

    float direction[3];
    for (s32 component = 0; component < 3; ++component) {
        direction[component] = titleDirections ? static_cast<float>(titleLight1Direction[component])
                                               : -static_cast<float>(settings->light1Dir[component]);
    }

    const float length =
        std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (length < 1.0f) {
        if (titleDirections) {
            // Title night slots intentionally author a zero direction, which must clear the old
            // diffuse direction rather than holding it from the preceding slot.
            direction[0] = direction[1] = direction[2] = 0.0f;
            Zelda3D_GL_SetLightDir(direction);
        }
        return;
    }

    direction[0] /= length;
    direction[1] /= length;
    direction[2] /= length;
    gZelda3dLightDirLast[0] = direction[0];
    gZelda3dLightDirLast[1] = direction[1];
    gZelda3dLightDirLast[2] = direction[2];
    Zelda3D_GL_SetLightDir(direction);
}

void Zelda3D_UpdateSceneLighting(PlayState* play) {
    Zelda3D_SelectScenePalette(play);

    int8_t titleLight1Direction[3] = { 0, 0, 0 };
    int8_t titleLight2Direction[3] = { 0, 0, 0 };
    const bool titleDirections = Zelda3D_TitleLightDirections(titleLight1Direction, titleLight2Direction);
    const EnvLightSettings* settings = &play->envCtx.lightSettings;
    Zelda3D_SubmitSceneLightColors(settings, titleDirections, titleLight2Direction);
    Zelda3D_SubmitPrimaryLightDirection(settings, titleDirections, titleLight1Direction);
}
