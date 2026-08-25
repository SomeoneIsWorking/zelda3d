#include "../lighting/zelda3d_lighting.h"
#include "scene_tint.h"

#include <cstdlib>

namespace {

void ApplyTintEnvironmentOverrides() {
    static bool initialized = false;
    if (initialized) {
        return;
    }

    const char* diffuse = std::getenv("ZELDA3D_TINT_DIFF");
    const char* multiplier = std::getenv("ZELDA3D_TINT_MUL");
    if (diffuse != nullptr && diffuse[0] != '\0') {
        gZelda3dTintDiff = static_cast<float>(std::atof(diffuse));
    }
    if (multiplier != nullptr && multiplier[0] != '\0') {
        gZelda3dTintMul = static_cast<float>(std::atof(multiplier));
    }
    initialized = true;
}

} // namespace

void Zelda3D_SceneTint(PlayState* play, u8 out[3]) {
    ApplyTintEnvironmentOverrides();

    EnvLightSettings* settings = &play->envCtx.lightSettings;
    for (int component = 0; component < 3; component++) {
        // This tint approximates the 3DS lit result for converted (unlit-dlist) OoT3D content, so
        // it reads the OoT3D color blend when one is live. Fallback: title / no-palette scenes.
        float ambient = gZelda3dEnvColors.valid ? gZelda3dEnvColors.amb[component] * 255.0f
                                                : static_cast<float>(settings->ambientColor[component]);
        float light1 = gZelda3dEnvColors.valid ? gZelda3dEnvColors.l1col[component] * 255.0f
                                               : static_cast<float>(settings->light1Color[component]);
        float light2 = gZelda3dEnvColors.valid ? gZelda3dEnvColors.l2col[component] * 255.0f
                                               : static_cast<float>(settings->light2Color[component]);
        float value = (ambient + gZelda3dTintDiff * (light1 + light2)) * gZelda3dTintMul;
        out[component] = (value <= 0.0f) ? 0 : (value >= 255.0f) ? 255 : static_cast<u8>(value + 0.5f);
    }
}
