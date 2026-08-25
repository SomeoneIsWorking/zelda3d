// N64-style world fog and OoT3D PICA distance-fog state.

#include "fast/zelda3d_fog.h"

#include <cmath>

extern "C" int gZelda3dFogEnable = 0;
extern "C" int gZelda3dFogOverride = 0;
extern "C" float gZelda3dFogColor[3] = { 0.0f, 0.0f, 0.0f };
extern "C" float gZelda3dFogMul = 0.0f;
extern "C" float gZelda3dFogOffset = 0.0f;
extern "C" int gZelda3dFog3dOn = 0;
extern "C" float gZelda3dFog3d[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
extern "C" int gZelda3dFog3dForceOff = 0;

extern "C" void Zelda3D_Fog3dSet(float cameraNear, float zFar, float fogNear, float fogFar, const float eyeWorld[3],
                                 const float forwardWorld[3]) {
    if (gZelda3dFog3dForceOff || !(zFar > cameraNear) || !(fogFar > fogNear)) {
        gZelda3dFog3dOn = 0;
        return;
    }

    const float projectionA = zFar / (zFar - cameraNear);
    const float projectionB = cameraNear * projectionA;
    float forward[3] = { forwardWorld[0], forwardWorld[1], forwardWorld[2] };
    const float length = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (length < 1e-6f) {
        gZelda3dFog3dOn = 0;
        return;
    }
    for (float& component : forward) {
        component /= length;
    }

    gZelda3dFog3d[0] = projectionA;
    gZelda3dFog3d[1] = projectionB;
    gZelda3dFog3d[2] = fogNear;
    gZelda3dFog3d[3] = fogFar;
    gZelda3dFog3d[4] = forward[0];
    gZelda3dFog3d[5] = forward[1];
    gZelda3dFog3d[6] = forward[2];
    gZelda3dFog3d[7] = forward[0] * eyeWorld[0] + forward[1] * eyeWorld[1] + forward[2] * eyeWorld[2];
    gZelda3dFog3dOn = 1;
}

extern "C" void Zelda3D_Fog3dOff() {
    gZelda3dFog3dOn = 0;
}
