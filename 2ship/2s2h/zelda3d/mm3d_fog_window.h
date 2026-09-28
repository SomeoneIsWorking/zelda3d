// MM3D's PICA distance fog: the WINDOW, fed from the recovered 3DS scene palette.
//
// WHAT THIS IS, PRECISELY, BECAUSE "MM fog" IS NOT ONE THING. The window is the part that decides the
// fog CURVE -- the 128-entry table PICA samples. It is fully determined by (zFar, fogNear, fogFar,
// cameraNear), and MM3D's recovered record supplies the first three while the last is a projection
// constant. That makes the window a PREDICTION rather than a fit, and it is validated: `z2_lost_woods`
// slot 2's window reproduces MM3D's own authored PICA LUT to 1.19 byte steps
// (`tools/mm3d_fog_prediction.py`, against a shuffled-LUT control of 0.02117).
//
// The COLOUR is a different part and is NOT here. MM's colour blend is ADDITIVE --
// `list[slot] + spA4[...]` for four sources, where `spA4` is `adjLightSettings`, the DIFFERENCE
// between adjacent light settings -- so the result lies outside the convex hull of the table's own
// colours and no interpolation of the table reproduces it. That is a missing INPUT, not a missing
// implementation, and pretending otherwise would be a second, wrong blend. So this module moves the
// window and leaves the colour to MM's own blend, which already operates on 3DS data via the
// substituted list.
//
// The blend RULE is the shared one (`Zelda3D_EnvBlendWindow`), so this file contains no blend
// arithmetic at all -- Majora's Mask and Ocarina of Time run the same function over different tables.
#ifndef MM3D3D_FOG_WINDOW_H
#define MM3D3D_FOG_WINDOW_H

#include "ultra64.h"

// The slot record type, for Mm3d_SceneFogSlot's return value. Game-agnostic: both games' generated
// tables are filled from this one type.
#include "lighting/zelda3d_env_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Majora's Mask's 3DS camera near plane.
 *
 * NOT a scene value and NOT shared with Ocarina of Time: it is a projection constant, and it was
 * measured by recovering it from MM3D's own authored fog LUT (the only way to see it, since no scene
 * record stores it). MM3D's is ~77 where OoT3D's measured gameplay value is 7.0, so a shared default
 * would be wrong here by an order of magnitude and would change the curve's shape, not just its
// scale. If MM3D's projection ever changes, this constant is the thing that must be re-measured.
 */
#define MM3D3D_CAMERA_NEAR 77.0f

/// Install MM3D's PICA fog window for this frame, or turn the fog off. Called every frame.
///
/// No-ops (fog OFF) when the frame has no captured schedule, the scene has no recovered palette, the
/// captured indices are out of range, or the blended window is degenerate. Each of those is a
/// distinct state and they all end in the same place on purpose: a wrong window renders as fog, and
/// no fog is the honest answer when the window is unknown.
void Mm3d_UpdateFogWindow(PlayState* play);

/// Diagnostics: whether the fog window was installed this frame, and the values it used.
bool Mm3d_QueryFogWindow(float* outFogNear, float* outFogFar, float* outZFar, float* outCameraNear);

/**
 * The recovered 3DS fog distances for one slot of one scene, straight from the generated table.
 *
 * The independent read. `Mm3d_QueryFogWindow` answers "what did the renderer get", which has been
 * through the capture, the blend and the feed; this answers "what does the recovered 3DS record say",
 * which has not. A diagnostic that only reports the first cannot tell a correct value from a
 * same-shaped wrong one, so callers are expected to compare the two.
 *
 * NULL when the scene has no recovered palette or `slot` is out of range -- a caller must not read
 * through it.
 */
const Zelda3dLightSlot* Mm3d_SceneFogSlot(s16 sceneId, u8 slot);

#ifdef __cplusplus
}
#endif

#endif // MM3D3D_FOG_WINDOW_H
