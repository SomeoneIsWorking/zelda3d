#include "model_tuning.h"
#include "model_tuning_query.h"

float gZelda3dRotX = 0.0f;
float gZelda3dRotY = 0.0f;
float gZelda3dRotZ = 0.0f;
int gZelda3dSwTilt = 1;
float gZelda3dGScale[32] = { 0 };
float gZelda3dAutoYoffNudge = 0.0f;

int Zelda3D_ModelScaleOverride(int slot, float* outScale) {
    if (slot < 0 || slot >= 32 || gZelda3dGScale[slot] <= 0.0f) {
        return 0;
    }
    if (outScale != 0) {
        *outScale = gZelda3dGScale[slot];
    }
    return 1;
}

float Zelda3D_ModelScaleOrDefault(int slot, float fallback) {
    float scale = fallback;
    Zelda3D_ModelScaleOverride(slot, &scale);
    return scale;
}

float Zelda3D_ModelAutoYOffsetNudge(void) {
    return gZelda3dAutoYoffNudge;
}

void Zelda3D_ModelRotationDegrees(float* outX, float* outY, float* outZ) {
    if (outX != 0) {
        *outX = gZelda3dRotX;
    }
    if (outY != 0) {
        *outY = gZelda3dRotY;
    }
    if (outZ != 0) {
        *outZ = gZelda3dRotZ;
    }
}
