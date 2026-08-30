// Internal per-model lighting overrides applied when a draw is submitted.
#ifndef ZELDA3D_FAST_LIGHTING_STATE_H
#define ZELDA3D_FAST_LIGHTING_STATE_H

namespace Zelda3DFast {

struct LightingOverrides {
    bool hasLightDirection = false;
    float lightDirection[3] = { 0.0f, 0.0f, 1.0f };
    bool hasSphereMapNormalMatrix = false;
    float sphereMapNormalMatrix[9] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };
};

LightingOverrides LightingOverridesForDraw(int modelId);
void EvictLightingOverrides(int firstModelId, int endModelId);

} // namespace Zelda3DFast

#endif // ZELDA3D_FAST_LIGHTING_STATE_H
