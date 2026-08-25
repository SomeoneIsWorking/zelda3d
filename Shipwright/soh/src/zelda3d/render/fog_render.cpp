#include "fog_render.h"

#include <fast/zelda3d_fog.h>

#include <cstdint>

// Convert an F3DEX fog position pair (min,max in the 0..1000 projected-depth scale, exactly what
// z_play.c passes to gSPFogPosition) into the (fogMul, fogOffset) the RSP fog stage uses. Mirrors
// the gbi.h gSPFogPosition macro and the s16 truncation the interpreter reads back, so the world
// shaders reproduce the N64/OoT3D fog curve bit-for-bit. Stored into the shared fog globals.
void Zelda3D_FogSetPosition(float fmin, float fmax) {
    float span = fmax - fmin;
    if (span < 1.0f) {
        span = 1.0f; // avoid div-by-zero / inverted positions
    }
    // gSPFogPosition: fm = 128000/(max-min), fo = (500-min)*256/(max-min). The macro casts to s32
    // and the word is read back as int16_t (interpreter.cpp G_RDPSETOTHERMODE_H/G_MOVEWORD fog).
    gZelda3dFogMul = static_cast<float>(static_cast<int16_t>(static_cast<int>(128000.0f / span)));
    gZelda3dFogOffset = static_cast<float>(static_cast<int16_t>(static_cast<int>((500.0f - fmin) * 256.0f / span)));
}

void Zelda3D_UpdateFog(PlayState* play) {
    if (gZelda3dFogOverride) {
        return;
    }

    const EnvLightSettings* settings = &play->envCtx.lightSettings;
    // Fog COLOUR comes straight from the live (time-blended) scene env (N64 OTR scene data).
    gZelda3dFogColor[0] = static_cast<float>(settings->fogColor[0]) / 255.0f;
    gZelda3dFogColor[1] = static_cast<float>(settings->fogColor[1]) / 255.0f;
    gZelda3dFogColor[2] = static_cast<float>(settings->fogColor[2]) / 255.0f;

    // F3DEX gSPFogPosition(fogNear, 1000) -> (fogMul, fogOffset). The Zelda3D world shader applies
    // that ramp to the OoT3D mesh's GL NDC depth, so its far endpoint must be the scene's real
    // fogFar. A hardcoded 1000 collapses the span for scenes such as Kokiri and turns the gradual
    // distance haze into a near-step.
    const float fogFar = settings->fogFar > settings->fogNear + 1 ? static_cast<float>(settings->fogFar) : 1000.0f;
    Zelda3D_FogSetPosition(static_cast<float>(settings->fogNear), fogFar);
}
