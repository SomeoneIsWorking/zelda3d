// The environment-light BLEND SCHEDULE, captured from the N64 code, and the scene-lighting window
// blend derived from it. GAME-AGNOSTIC -- this is the shared owner both games use.
//
// WHY SHARED, AND WHY IT IS NOT A BLEND REIMPLEMENTATION. Both N64 games compute the environment
// blend the same way -- two source slots lerped by a time weight, that pair lerped by a config weight
// -- and both already do it in their own `z_kankyo`. So this module does NOT reimplement that blend.
// It captures the SCHEDULE (which four slots, and the two weights) at the point where the game's own
// code has just computed it, and then evaluates the ONE thing the renderer needs and the game does
// not provide: the PICA fog window.
//
// The window is the right thing to share because the rule is title-neutral and the DATA is not: each
// game's table supplies its own slots, and the two-LERP-over-four-slots rule is identical. A copy per
// game would be a second implementation of one rule, which is the failure this codebase keeps
// refusing to add.
//
// COLOURS ARE NOT HERE, AND THE OMISSION IS LOAD-BELOWING. Majora's Mask's colour blend is ADDITIVE --
// `list[slot] + spA4[...]` for four sources (`2ship/src/code/z_kankyo.c:1394`), where `spA4` is
// `adjLightSettings`, the DIFFERENCE between adjacent light settings. Adding a difference moves the
// result outside the convex hull of the raw slot values, so no interpolation of the table reproduces
// MM's colours, and this module deliberately does not pretend otherwise: it blends the window only.
// MM's colours come out of the game's own blend, from the substituted list, with no arithmetic here.
#ifndef ZELDA3D_SHARED_LIGHTING_ZELDA3D_ENV_BLEND_H
#define ZELDA3D_SHARED_LIGHTING_ZELDA3D_ENV_BLEND_H

#include "lighting/zelda3d_env_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The blend the N64 code just performed, captured so the renderer can replay it on 3DS data.
 *
 * `idx[0..1]` are the two slots of the first time-blend and `idx[2..3]` those of the second, in the
 * order the game's own code used them; `wTime` lerps within each pair and `wConfig` lerps between
 * them. `timeBased` is 0 for the non-time-driven path, where the game leaves the direction alone.
 *
 * `valid` is 0 whenever no blend has been captured, which is a different state from "captured with
 * weight 0" and must not be collapsed: a caller that treats an uncaptured frame as wTime=0 would
 * silently light every scene with slot 0's palette.
 */
typedef struct {
    unsigned char valid;
    unsigned char timeBased;
    unsigned char idx[4];
    float wTime;
    float wConfig;
} Zelda3dEnvBlend;

extern Zelda3dEnvBlend gZelda3dEnvBlend;

/**
 * Record the blend the N64 code just performed.
 *
 * A function rather than a struct assignment so the caller's decomp seam grows by ONE line instead of
 * a block: `z_kankyo.c` is a legacy seam under a size limit, and inlining the capture there grew it
 * from 3728 to 3762 lines and failed the structure check. The rule is the point -- new behaviour
 * belongs in a focused module and the seam composes.
 *
 * `firstA`/`firstB` are the pair `wTime` lerps and `secondA`/`secondB` the pair `wConfig` then lerps
 * between, in the order the game's own colour loops used them.
 *
 * THE ORDERING IS PART OF THE CONTRACT, AND THE TWO GAMES DIFFER -- which is the whole reason the
 * indices are captured instead of derived. The rule below is `Lerp(Lerp(firstA, firstB, wTime),
 * Lerp(secondA, secondB, wTime), wConfig)`, so the caller must pass the slot that sits at weight 0
 * FIRST. OoT passes `TIME_ENTRY_1F.unk_04, .unk_05`, matching the `LERP(list[unk_04], list[unk_05],
 * sp8C)` immediately below its own capture. MM passes `sp97, sp95`, because MM evaluates the same pair
 * the other way round: `Environment_LerpColor(to, from, w)` returns `from + (to - from) * w` and
 * `S16_LERP(a, b, w)` is `b + (a - b) * w`, so with `LerpColor(list[sp95], list[sp97], w)` the weight-0
 * slot is `sp97`. Capturing MM as `(sp95, sp97, ...)` therefore made this rule lerp both pairs
 * BACKWARDS -- which fed the renderer the wrong end of MM's fog blend while producing a perfectly
 * ordinary-looking window. Both orders are valid lerps, so no shape-based test can see it; only
 * comparing the shared rule's result against the value the game's own code computed for the same
 * frame can, which is what `fog` now reports.
 */
void Zelda3D_EnvBlendCapture(int firstA, int firstB, int secondA, int secondB, float wTime, float wConfig);

/**
 * The blended PICA fog window for one scene's palette, or 0 if it cannot be computed.
 *
 * Returns non-zero and fills the four outputs when the captured schedule is usable for `palette`:
 * `valid` set, every index inside the palette, and the game's own ordering of near-before-far. The
 * ordering check matters because the 3DS stores far and near where the N64 struct stores the packed
 * `blendRateAndFogNear`; a window with near at or beyond far would ramp the wrong way and still look
 * like fog.
 */
int Zelda3D_EnvBlendWindow(const Zelda3dSceneLight* palette, float* outFogNear, float* outFogFar, float* outZFar,
                           float* outBlendRate);

/**
 * The blended window as the four values `Zelda3D_Fog3dSet` takes, plus whether it is usable.
 *
 * `outCameraNear` is left alone on failure. Split out from the window itself because the camera near
 * plane is NOT scene data: it is a projection constant, and it differs per title (measured: OoT3D 7.0,
 * MM3D ~77) so the caller supplies it rather than this module guessing.
 */
int Zelda3D_EnvBlendFogWindow(const Zelda3dSceneLight* palette, float* outFogNear, float* outFogFar, float* outZFar);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_SHARED_LIGHTING_ZELDA3D_ENV_BLEND_H
