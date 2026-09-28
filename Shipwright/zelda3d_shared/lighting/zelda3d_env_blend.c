// The environment-light blend schedule and the scene-lighting window blend. See
// zelda3d_env_blend.h for why this is shared and why it blends the window but not the colours.

#include "lighting/zelda3d_env_blend.h"

// NULL is used for the "not requested" out-parameter; the shared target compiles as C++ with no
// prelude that provides it.
#include <stddef.h>

Zelda3dEnvBlend gZelda3dEnvBlend;

/// The N64 rule, once: lerp within each time-pair by the time weight, then lerp between the pairs by
/// the config weight. `field` picks which member of the slot to read, so the rule is written once for
/// all three window values instead of three times.
static float Zelda3D_Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/// A captured weight can fall outside [0,1] at the ends of a transition, and an unclamped weight
/// extrapolates the window into a nonsense ramp that still renders as fog.
static float Zelda3D_Clamp01(float v) {
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

void Zelda3D_EnvBlendCapture(int firstA, int firstB, int secondA, int secondB, float wTime, float wConfig) {
    gZelda3dEnvBlend.valid = 1;
    gZelda3dEnvBlend.timeBased = 1;
    // Stored through a mask rather than a cast: a game whose palette is larger than 256 slots would
    // otherwise wrap an index and select a different slot than the game's own code just used.
    gZelda3dEnvBlend.idx[0] = (unsigned char)(firstA & 0xFF);
    gZelda3dEnvBlend.idx[1] = (unsigned char)(firstB & 0xFF);
    gZelda3dEnvBlend.idx[2] = (unsigned char)(secondA & 0xFF);
    gZelda3dEnvBlend.idx[3] = (unsigned char)(secondB & 0xFF);
    gZelda3dEnvBlend.wTime = wTime;
    gZelda3dEnvBlend.wConfig = wConfig;
}

typedef float (*Zelda3dSlotField)(const Zelda3dLightSlot*);

static float Zelda3dSlot_fogNear(const Zelda3dLightSlot* s) {
    return (float)s->fogNear;
}
static float Zelda3dSlot_fogFar(const Zelda3dLightSlot* s) {
    return s->fogFar;
}
static float Zelda3dSlot_zFar(const Zelda3dLightSlot* s) {
    return s->zFar;
}

/// Blend one field of the window over the captured schedule. Returns 0 when the schedule does not
/// apply to this palette, so a caller can fall back rather than render a window from slot 0.
static int Zelda3D_BlendWindowField(const Zelda3dSceneLight* palette, Zelda3dSlotField field, float* out) {
    const Zelda3dEnvBlend* blend = &gZelda3dEnvBlend;
    if (palette == NULL || palette->slots == NULL || palette->numSlots == 0 || !blend->valid) {
        return 0;
    }
    const int slots = (int)palette->numSlots;
    for (int i = 0; i < 4; i++) {
        if ((int)blend->idx[i] >= slots) {
            // Out of range: reading past the palette would be exactly the "leave the N64 values
            // rather than read past the array" case, and an out-of-range index is a memory error, not
            // a fallback.
            return 0;
        }
    }

    const float wTime = Zelda3D_Clamp01(blend->wTime);
    const float wConfig = Zelda3D_Clamp01(blend->wConfig);
    const float first =
        Zelda3D_Lerp(field(&palette->slots[blend->idx[0]]), field(&palette->slots[blend->idx[1]]), wTime);
    const float second =
        Zelda3D_Lerp(field(&palette->slots[blend->idx[2]]), field(&palette->slots[blend->idx[3]]), wTime);
    *out = Zelda3D_Lerp(first, second, wConfig);
    return 1;
}

int Zelda3D_EnvBlendWindow(const Zelda3dSceneLight* palette, float* outFogNear, float* outFogFar, float* outZFar,
                           float* outBlendRate) {
    float fogNear;
    float fogFar;
    float zFar;
    if (!Zelda3D_BlendWindowField(palette, Zelda3dSlot_fogNear, &fogNear) ||
        !Zelda3D_BlendWindowField(palette, Zelda3dSlot_fogFar, &fogFar) ||
        !Zelda3D_BlendWindowField(palette, Zelda3dSlot_zFar, &zFar)) {
        return 0;
    }
    if (!(fogFar > fogNear) || !(zFar > 0.0f)) {
        // A degenerate window is reachable at a transition boundary. Feeding it on would ramp the
        // wrong way, so refusing here is what keeps the fog OFF rather than inverted -- and it is
        // decided from the DATA rather than left to the host's own guard downstream.
        return 0;
    }
    *outFogNear = fogNear;
    *outFogFar = fogFar;
    *outZFar = zFar;
    if (outBlendRate != NULL) {
        *outBlendRate = Zelda3D_Clamp01(gZelda3dEnvBlend.wConfig);
    }
    return 1;
}

int Zelda3D_EnvBlendFogWindow(const Zelda3dSceneLight* palette, float* outFogNear, float* outFogFar, float* outZFar) {
    return Zelda3D_EnvBlendWindow(palette, outFogNear, outFogFar, outZFar, NULL);
}
