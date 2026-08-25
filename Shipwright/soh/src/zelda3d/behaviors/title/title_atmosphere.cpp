#include "title_atmosphere.h"

#include "title_activity.h"
#include "../../cutscene/zelda3d_cutscene.h"
#include "functions/environment.h"

namespace Zelda3D {

void UpdateTitleAtmosphere(PlayState* play) {
    uint16_t dayTime = 0;
    Zelda3D_TitleCsTimeOfDay(static_cast<float>(Zelda3D_TitleCsFrame()) + Zelda3D_TitleCsSubframe(), &dayTime);
    gSaveContext.dayTime = dayTime;
    gSaveContext.skyboxTime = dayTime;

    play->envCtx.sunMoonDisabled = false;
    play->envCtx.skyboxDisabled = false;
    play->skyboxId = SKYBOX_NORMAL_SKY;
    if (play->envCtx.skybox1Index == 99 || play->envCtx.skybox2Index == 99) {
        Environment_UpdateSkybox(play, play->skyboxId, &play->envCtx, &play->skyboxCtx);
    }
}

} // namespace Zelda3D

extern "C" void Zelda3D_Title_ApplyDomeOverride(PlayState* play) {
    if (!Zelda3D_Title_IsActive() || play == nullptr) {
        return;
    }

    uint16_t dayTime;
    if (!Zelda3D_TitleCsTimeOfDay(static_cast<float>(Zelda3D_TitleCsFrame()) + Zelda3D_TitleCsSubframe(), &dayTime)) {
        return;
    }

    int firstDome;
    int secondDome;
    float blend;
    if (!Zelda3D_TitleCsDomeBlend(dayTime, &firstDome, &secondDome, &blend)) {
        return;
    }
    play->envCtx.skybox1Index = static_cast<uint8_t>(firstDome);
    play->envCtx.skybox2Index = static_cast<uint8_t>(secondDome);
    play->envCtx.skyboxBlend = static_cast<uint8_t>(blend * 255.0f + 0.5f);
}
