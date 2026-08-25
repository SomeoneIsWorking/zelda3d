// Shared height-measurement state machine for actor replacement models.
#ifndef ZELDA3D_RENDER_ACTOR_HEIGHT_CALIBRATION_H
#define ZELDA3D_RENDER_ACTOR_HEIGHT_CALIBRATION_H

#include "global.h"

struct Zelda3D_HeightCalibration {
    float measuredHeight;
    float scale;
    int modelId;
    signed char state;
    signed char tries;
};

struct Zelda3D_HeightCalibratedModel {
    const char* modelKey;
    const char* diagnosticName;
    int overrideSlot;
    int measureKey;
    int maxTries;
    float fallbackScale;
    bool drawFallback;
};

int Zelda3D_TryDrawHeightCalibratedModel(PlayState* play, Actor* actor, Zelda3D_HeightCalibration* calibration,
                                         const Zelda3D_HeightCalibratedModel& model);
void Zelda3D_RecordHeightCalibration(Zelda3D_HeightCalibration* calibration, float height);
int Zelda3D_RetryHeightCalibration(Zelda3D_HeightCalibration* calibration);

#endif // ZELDA3D_RENDER_ACTOR_HEIGHT_CALIBRATION_H
