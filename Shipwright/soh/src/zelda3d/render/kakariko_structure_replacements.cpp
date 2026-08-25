#include "kakariko_structure_replacements.h"

#include "../behaviors/actor/actor_assets.h"
#include "../diagnostics/model_tuning_query.h"
#include "actor_model_submission.h"
#include "actor_height_calibration.h"
#include "model_queries.h"
#include "replacement_calibration.h"

namespace {

Zelda3D_HeightCalibration sWindmillCalibration = {};
Zelda3D_HeightCalibration sWellArchCalibration = {};

int DrawWellWater(PlayState* play, Actor* actor) {
    const int modelId = Zelda3D_AutoModelId(ZSPOT01 "|c_s01idomizu");
    float scale = Zelda3D_ModelScaleOrDefault(9, ZELDA3D_SPOT01_WORLD_SCALE);
    float ignoredScale = 0.0f;
    const int hasScaleOverride = Zelda3D_ModelScaleOverride(9, &ignoredScale);
    CollisionHeader* collision = play->colCtx.colHeader;
    float extentX = 0.0f;
    float extentZ = 0.0f;
    if (!hasScaleOverride && collision != nullptr && collision->numWaterBoxes > 0 &&
        collision->waterBoxes != nullptr && Zelda3D_AutoModelExtentXZ(modelId, &extentX, &extentZ) && extentX > 1e-3f &&
        extentZ > 1e-3f) {
        WaterBox& waterBox = collision->waterBoxes[0];
        if (waterBox.xLength > 0 && waterBox.zLength > 0) {
            // The water CMB is flat, so height calibration is undefined. Its owning waterbox is the
            // authoritative N64 surface rectangle and supplies both footprint dimensions.
            scale = 0.5f * ((float)waterBox.xLength / extentX + (float)waterBox.zLength / extentZ);
        }
    }
    Zelda3D_DrawModelGL(play, modelId, actor, scale, nullptr, 0.0f, nullptr, nullptr);
    return 1;
}

} // namespace

int Zelda3D_TryDrawKakarikoStructureReplacement(PlayState* play, Actor* actor) {
    // These actors share object banks, but each maps to a distinct authored CMB. Keeping their
    // selection here prevents the generic largest-model heuristic from confusing the structures.
    if (actor->id == ACTOR_BG_SPOT01_FUSYA) {
        static const Zelda3D_HeightCalibratedModel windmill = {
            ZSPOT01 "|c_s01fusya",
            "windmill (c_s01fusya)",
            7,
            ZELDA3D_MEASKEY_WINDMILL,
            8,
            ZELDA3D_SPOT01_WORLD_SCALE,
            true,
        };
        return Zelda3D_TryDrawHeightCalibratedModel(play, actor, &sWindmillCalibration, windmill);
    }
    if (actor->id == ACTOR_BG_SPOT01_IDOHASHIRA) {
        static const Zelda3D_HeightCalibratedModel wellArch = {
            ZSPOT01 "|c_s01idohashira",
            "well-arch (c_s01idohashira)",
            8,
            ZELDA3D_MEASKEY_WELLARCH,
            8,
            ZELDA3D_SPOT01_WORLD_SCALE,
            true,
        };
        return Zelda3D_TryDrawHeightCalibratedModel(play, actor, &sWellArchCalibration, wellArch);
    }
    if (actor->id == ACTOR_BG_SPOT01_IDOMIZU) {
        return DrawWellWater(play, actor);
    }
    if (actor->id == ACTOR_BG_GATE_SHUTTER) {
        Zelda3D_DrawModelGL(play, Zelda3D_AutoModelId(ZMATOYAB "|c_s01tomegate"), actor,
                            Zelda3D_ModelScaleOrDefault(10, ZELDA3D_MATOYAB_WORLD_SCALE), nullptr, 0.0f, nullptr,
                            nullptr);
        return 1;
    }
    return -1;
}

int Zelda3D_KakarikoStructureRecordMeasure(int key, float height) {
    if (key == ZELDA3D_MEASKEY_WINDMILL) {
        Zelda3D_RecordHeightCalibration(&sWindmillCalibration, height);
        return 1;
    }
    if (key == ZELDA3D_MEASKEY_WELLARCH) {
        Zelda3D_RecordHeightCalibration(&sWellArchCalibration, height);
        return 1;
    }
    return 0;
}

int Zelda3D_KakarikoStructureRetryNoMeasurement(void) {
    return Zelda3D_RetryHeightCalibration(&sWindmillCalibration) +
           Zelda3D_RetryHeightCalibration(&sWellArchCalibration);
}
