#include "actor_height_calibration.h"

#include "actor_model_submission.h"
#include "../diagnostics/model_tuning_query.h"
#include "model_queries.h"
#include "replacement_calibration.h"
#include "replacement_control.h"

#include <cstdio>

int Zelda3D_TryDrawHeightCalibratedModel(PlayState* play, Actor* actor, Zelda3D_HeightCalibration* calibration,
                                         const Zelda3D_HeightCalibratedModel& model) {
    if (calibration->modelId == 0) {
        calibration->modelId = Zelda3D_AutoModelId(model.modelKey);
        if (calibration->modelId < 0) {
            calibration->state = 3;
        }
    }
    if (calibration->modelId < 0) {
        return 0;
    }

    float scale = model.fallbackScale;
    const int hasScaleOverride = Zelda3D_ModelScaleOverride(model.overrideSlot, &scale);
    if (hasScaleOverride) {
    } else if (calibration->state == 2) {
        scale = calibration->scale;
    } else if (calibration->state != 3 && calibration->state != ZELDA3D_AUTO_NOMEAS) {
        if (calibration->measuredHeight > 0.0f) {
            const float modelHeight = Zelda3D_AutoModelHeight(calibration->modelId);
            if (modelHeight > 1e-3f) {
                calibration->scale = calibration->measuredHeight / modelHeight;
                calibration->state = 2;
                scale = calibration->scale;
                if (Zelda3D_AutoMode() >= 1) {
                    fprintf(stderr, "SOH3D AUTO: %s -> scale=%.5f (n64h=%.1f modelh=%.1f)\n", model.diagnosticName,
                            scale, calibration->measuredHeight, modelHeight);
                    fflush(stderr);
                }
            } else {
                calibration->state = 3;
            }
        } else if (calibration->tries < model.maxTries) {
            calibration->tries++;
            calibration->state = 1;
            Zelda3D_BeginReplacementMeasurement(play, model.measureKey);
            return 0;
        } else {
            calibration->state = ZELDA3D_AUTO_NOMEAS;
        }
    }

    if (calibration->state != 2 && !model.drawFallback && !hasScaleOverride) {
        return 0;
    }
    Zelda3D_DrawModelGL(play, calibration->modelId, actor, scale, nullptr, 0.0f, nullptr, nullptr);
    return 1;
}

void Zelda3D_RecordHeightCalibration(Zelda3D_HeightCalibration* calibration, float height) {
    calibration->measuredHeight = height;
}

int Zelda3D_RetryHeightCalibration(Zelda3D_HeightCalibration* calibration) {
    if (calibration->state != ZELDA3D_AUTO_NOMEAS) {
        return 0;
    }
    calibration->state = 0;
    calibration->tries = 0;
    calibration->measuredHeight = 0.0f;
    return 1;
}
