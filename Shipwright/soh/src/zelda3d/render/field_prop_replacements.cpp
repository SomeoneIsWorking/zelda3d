#include "field_prop_replacements.h"

#include "../behaviors/actor/actor_assets.h"
#include "../diagnostics/model_tuning_query.h"
#include "actor_model_submission.h"
#include "actor_height_calibration.h"
#include "model_queries.h"
#include "replacement_calibration.h"
#include "replacement_control.h"

#include <cstdio>

namespace {

struct VariantReplacement {
    s16 actorId;
    u16 paramMask;
    u16 paramValue;
    int modelId;
    float fallbackScale;
    bool selfCalibrate;
    Zelda3D_HeightCalibration calibration;
    float measuredFootprintX;
    float measuredFootprintZ;
};

Zelda3D_HeightCalibration sFieldGrassCalibration = {};

// Obj_Hana and En_Ishi choose differently sized props from one object bank. Object-keyed auto
// calibration therefore cannot distinguish them. Only the two historically uncalibrated seeds are
// measured here: replacing the three playtest-calibrated values with this height-only measurement
// was previously shown to move them 17-31% away from their verified scales.
VariantReplacement sVariants[] = {
    { ACTOR_OBJ_HANA, 3, 2, 2, ZELDA3D_HANABUSH_WORLD_SCALE, false, {}, 0.0f, 0.0f },
    { ACTOR_OBJ_HANA, 3, 1, 4, ZELDA3D_ROCK_SMALL_WORLD_SCALE, false, {}, 0.0f, 0.0f },
    { ACTOR_OBJ_HANA, 3, 0, 6, ZELDA3D_FLOWER_WORLD_SCALE, true, {}, 0.0f, 0.0f },
    { ACTOR_EN_ISHI, 1, 0, 4, ZELDA3D_ROCK_SMALL_WORLD_SCALE, false, {}, 0.0f, 0.0f },
    { ACTOR_EN_ISHI, 1, 1, 5, ZELDA3D_ROCK_LARGE_WORLD_SCALE, true, {}, 0.0f, 0.0f },
};

int DrawVariant(PlayState* play, Actor* actor) {
    for (size_t index = 0; index < ARRAY_COUNT(sVariants); ++index) {
        VariantReplacement& variant = sVariants[index];
        if (variant.actorId != actor->id || ((u16)actor->params & variant.paramMask) != variant.paramValue) {
            continue;
        }

        Zelda3D_HeightCalibration& calibration = variant.calibration;
        if (calibration.state != 2 && variant.selfCalibrate) {
            if (calibration.measuredHeight > 0.0f) {
                const float modelHeight = Zelda3D_AutoModelHeight(variant.modelId);
                if (modelHeight > 1e-3f) {
                    calibration.scale = calibration.measuredHeight / modelHeight;
                    calibration.state = 2;
                    if (Zelda3D_AutoMode() >= 1) {
                        fprintf(stderr,
                                "SOH3D VARIANT: actor 0x%x params&0x%x==0x%x model %d -> scale=%.5f "
                                "(n64h=%.1f modelh=%.1f, seed was %.5f)\n",
                                (unsigned)actor->id, variant.paramMask, variant.paramValue, variant.modelId,
                                calibration.scale, calibration.measuredHeight, modelHeight, variant.fallbackScale);
                        fflush(stderr);
                    }
                }
            } else if (calibration.state != 3 && calibration.state != ZELDA3D_AUTO_NOMEAS) {
                if (calibration.tries < 64) {
                    calibration.tries++;
                    calibration.state = 1;
                    const int measureKey = (int)(ZELDA3D_MEASKEY_VARIANT_BASE + index);
                    Zelda3D_BeginReplacementMeasurement(play, measureKey);
                    return 0;
                }
                calibration.state = ZELDA3D_AUTO_NOMEAS;
            }
        }

        const float scale = calibration.state == 2 ? calibration.scale : variant.fallbackScale;
        const float groundOffset =
            -Zelda3D_AutoModelMinY(variant.modelId) + Zelda3D_ModelAutoYOffsetNudge();
        Zelda3D_DrawModelGL(play, variant.modelId, actor,
                            Zelda3D_ModelScaleOrDefault(variant.modelId, scale), nullptr, groundOffset, nullptr,
                            nullptr);
        return 1;
    }
    return -1;
}

} // namespace

int Zelda3D_TryDrawFieldPropReplacement(PlayState* play, Actor* actor) {
    if (actor->id == ACTOR_EN_KUSA && (actor->params & 3) == 0) {
        // Type zero is field grass spawned by Obj_Mure2, not the Kokiri bush used by En_Kusa types
        // one and two. Many instances share this calibration slot, hence the larger retry budget.
        static const Zelda3D_HeightCalibratedModel fieldGrass = {
            ZKEEP_FIELD "|grass05_model",
            "field-grass (grass05_model)",
            12,
            ZELDA3D_MEASKEY_FIELDGRASS,
            64,
            0.0f,
            false,
        };
        return Zelda3D_TryDrawHeightCalibratedModel(play, actor, &sFieldGrassCalibration, fieldGrass);
    }
    if (actor->id == ACTOR_OBJ_HANA || actor->id == ACTOR_EN_ISHI) {
        return DrawVariant(play, actor);
    }
    return -1;
}

int Zelda3D_FieldPropRecordMeasure(int key, float height, float footprintX, float footprintZ) {
    if (key == ZELDA3D_MEASKEY_FIELDGRASS) {
        Zelda3D_RecordHeightCalibration(&sFieldGrassCalibration, height);
        return 1;
    }
    if (key < ZELDA3D_MEASKEY_VARIANT_BASE || key >= ZELDA3D_MEASKEY_VARIANT_BASE + (int)ARRAY_COUNT(sVariants)) {
        return 0;
    }
    VariantReplacement& variant = sVariants[key - ZELDA3D_MEASKEY_VARIANT_BASE];
    Zelda3D_RecordHeightCalibration(&variant.calibration, height);
    variant.measuredFootprintX = footprintX;
    variant.measuredFootprintZ = footprintZ;
    return 1;
}

int Zelda3D_FieldPropRetryNoMeasurement(void) {
    int revived = Zelda3D_RetryHeightCalibration(&sFieldGrassCalibration);
    for (VariantReplacement& variant : sVariants) {
        revived += Zelda3D_RetryHeightCalibration(&variant.calibration);
    }
    return revived;
}

int Zelda3D_VariantSlotCount(void) {
    return (int)ARRAY_COUNT(sVariants);
}

const struct Zelda3D_ForcedMeasT* Zelda3D_VariantSlotInfoRaw(int index, short* outActorId,
                                                             unsigned short* outParamValue, int* outModelId,
                                                             float* outFallback, float* outScale, float* outMeasuredH,
                                                             int* outState, int* outTries) {
    if (index < 0 || index >= (int)ARRAY_COUNT(sVariants)) {
        return nullptr;
    }
    VariantReplacement& variant = sVariants[index];
    if (outActorId != nullptr) {
        *outActorId = variant.actorId;
    }
    if (outParamValue != nullptr) {
        *outParamValue = variant.paramValue;
    }
    if (outModelId != nullptr) {
        *outModelId = variant.modelId;
    }
    if (outFallback != nullptr) {
        *outFallback = variant.fallbackScale;
    }
    if (outScale != nullptr) {
        *outScale = variant.calibration.scale;
    }
    if (outMeasuredH != nullptr) {
        *outMeasuredH = variant.calibration.measuredHeight;
    }
    if (outState != nullptr) {
        *outState = variant.calibration.state;
    }
    if (outTries != nullptr) {
        *outTries = variant.calibration.tries;
    }
    return reinterpret_cast<const struct Zelda3D_ForcedMeasT*>(1);
}
