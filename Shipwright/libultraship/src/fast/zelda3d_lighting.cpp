// Scene lighting inputs and direct per-model lighting overrides.

#include "fast/zelda3d_lighting.h"

#include "zelda3d_lighting_state.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace Zelda3DFast {
namespace {

std::unordered_map<int, LightingOverrides> modelOverrides;

} // namespace

LightingOverrides LightingOverridesForDraw(int modelId) {
    const auto model = modelOverrides.find(modelId);
    return model == modelOverrides.end() ? LightingOverrides{} : model->second;
}

void EvictLightingOverrides(int firstModelId, int endModelId) {
    for (auto model = modelOverrides.begin(); model != modelOverrides.end();) {
        if (model->first >= firstModelId && model->first < endModelId) {
            model = modelOverrides.erase(model);
        } else {
            ++model;
        }
    }
}

} // namespace Zelda3DFast

extern "C" float gZelda3dLightDirWorld[3] = { 0.40f, 0.55f, 0.73f };
extern "C" float gZelda3dAmbient[3] = { 0.10f, 0.10f, 0.12f };
extern "C" float gZelda3dLight1Col[3] = { 0.80f, 0.75f, 0.65f };
extern "C" float gZelda3dLight2Dir[3] = { -0.40f, -0.55f, -0.73f };
extern "C" float gZelda3dLight2Col[3] = { 0.05f, 0.08f, 0.15f };
extern "C" float gZelda3dAmbientLightCount = 2.0f;
extern "C" float gZelda3dWorldAmbColor[3] = { 0.0f, 0.0f, 1.0f };
extern "C" float gZelda3dWorldAmb = 0.02f;
extern "C" int gZelda3dWorldAmbOverride = 1;

extern "C" void Zelda3D_GL_SetLightDir(const float direction[3]) {
    std::copy_n(direction, 3, gZelda3dLightDirWorld);
}

extern "C" void Zelda3D_GL_SetLightParams(const float ambient[3], const float light1Color[3],
                                          const float light2Direction[3], const float light2Color[3],
                                          int enabledLightCount) {
    std::copy_n(ambient, 3, gZelda3dAmbient);
    std::copy_n(light1Color, 3, gZelda3dLight1Col);
    std::copy_n(light2Direction, 3, gZelda3dLight2Dir);
    std::copy_n(light2Color, 3, gZelda3dLight2Col);
    gZelda3dAmbientLightCount = enabledLightCount > 0 ? static_cast<float>(enabledLightCount) : 1.0f;
}

extern "C" void Zelda3D_GL_SetLightDirOverride(int modelId, float x, float y, float z) {
    auto& override = Zelda3DFast::modelOverrides[modelId];
    override.hasLightDirection = true;
    override.lightDirection[0] = x;
    override.lightDirection[1] = y;
    override.lightDirection[2] = z;
}

extern "C" void Zelda3D_GL_ClearLightDirOverride(int modelId) {
    const auto model = Zelda3DFast::modelOverrides.find(modelId);
    if (model != Zelda3DFast::modelOverrides.end()) {
        model->second.hasLightDirection = false;
    }
}

extern "C" void Zelda3D_GL_SetSphereMapNormalMatrix(int modelId, const float matrix[9]) {
    auto& override = Zelda3DFast::modelOverrides[modelId];
    override.hasSphereMapNormalMatrix = true;
    std::copy_n(matrix, 9, override.sphereMapNormalMatrix);
}

extern "C" void Zelda3D_GL_ClearSphereMapNormalMatrix(int modelId) {
    const auto model = Zelda3DFast::modelOverrides.find(modelId);
    if (model != Zelda3DFast::modelOverrides.end()) {
        model->second.hasSphereMapNormalMatrix = false;
    }
}
