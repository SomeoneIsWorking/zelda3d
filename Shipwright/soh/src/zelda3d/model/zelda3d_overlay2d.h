// Zelda3D generic 2D screen-space overlay pass — a real orthographic draw pass for compositing
// flat/skinned CMB content over the finished 3D frame, independent of the 3D camera.
//
// WHY THIS EXISTS: SoH's only pre-existing 2D draw primitive is the N64 texrect blitter
// (zelda3d_hud_tex.cpp's textures, drawn via gSPTextureRectangle) — resolution-independent and
// screen-space by construction, but texrects are flat quads only; they cannot skin a bone
// hierarchy (title_logo_us.cmb is a 13-bone CSAB-driven model). This module is the matrix-pipeline
// equivalent: it swaps in an orthographic G_MTX_PROJECTION (built with the engine's own guOrtho,
// the same primitive z_view.c's View_ApplyOrtho / z_fbdemo_triforce.c's transition already use for
// N64's own 2D/UI passes) sized to a caller-chosen virtual reference resolution, so any ordinary
// gSPMatrix(MODELVIEW)+gSPZelda3DDraw* call becomes a screen-space placement with no camera-basis
// math at all — a translate to a pixel coordinate and a scale to a pixel height.
//
// USAGE: Begin() once per overlay pass, place + draw N models via Zelda3D_Overlay2D_PlaceModel
// (or do the translate/scale yourself in the same refW x refH pixel space), then End() to restore
// the ordinary 3D perspective projection for whatever draws next this frame.
//
// FIRST CONSUMER: the OoT3D title-demo overlay (behaviors/title/title_logo.cpp,
// title_fireglow.cpp) — see <oot3d-decomp>/docs/title_2d_overlay_logo.md §5.1. Written generic
// (not title-specific) because the port spec calls out that file-select and HUD elements will
// want the same primitive later.
#ifndef ZELDA3D_OVERLAY2D_H
#define ZELDA3D_OVERLAY2D_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Begins a screen-space orthographic pass over the virtual reference box [0,refW] x [0,refH]
// (top-left origin, Y DOWN — ordinary 2D screen convention), independent of play->view's camera.
// Loads a fresh G_MTX_PROJECTION onto POLY_OPA_DISP (NOPUSH) and disables the Z-buffer geometry
// mode (compositing order here is draw-call order, not a depth test against the already-finished
// 3D scene's Z-buffer contents, which are meaningless in this coordinate space). The real
// on-screen aspect/widescreen mapping is unchanged from the rest of the frame — this only swaps
// WHAT is projected, not the viewport the engine already set up this frame (same design as
// View_ApplyOrtho's use of the fixed 320x240 N64 virtual canvas for the pause/HUD ortho pass;
// here the virtual box is caller-chosen so the OoT3D title overlay can use ITS OWN native
// 400x240 top-screen convention directly, matching every oracle-measured screen-fraction
// constant in title_logo.cpp/title_fireglow.cpp with no unit conversion).
void Zelda3D_Overlay2D_Begin(PlayState* play, float refW, float refH);

// Places a model's MODELVIEW matrix (translate + a fixed orientation correction + uniform scale)
// in the SAME refW x refH pixel space Begin() established: center pixel (cxPx, cyPx), scaled so a
// model of local-space height `localHeight` renders at `heightPx` screen pixels tall. Loads the
// matrix (G_MTX_LOAD) onto POLY_OPA_DISP; caller issues the actual gSPZelda3DDraw* call
// immediately after. No-op (does not touch the display list) if localHeight <= 0.
//
// Orientation is a FIXED constant (zelda3d_overlay2d.cpp's kOverlayFixedRotX), not derived from
// the 3D camera. This was tried the other way first — composing with play->billboardMtxF (the
// live title-cs camera's own rotation), on the theory that the decompiled title-logo actor's
// draw fn genuinely uses "the overlay's existing camera-basis technique" (oot3d-decomp/docs/
// title_logo_actor.md §6.1) — and that was empirically FALSIFIED: it only looked correct at the
// one cs frame it was tuned against, and flipped the wordmark upside-down at a different camera
// angle later in the same cutscene (see zelda3d_overlay2d.cpp's comment for the full derivation).
// A fixed constant is the geometrically correct read of that decomp finding once the camera this
// pass projects through is itself fixed (ortho, not the moving title-cs camera).
void Zelda3D_Overlay2D_PlaceModel(PlayState* play, float cxPx, float cyPx, float heightPx,
                                   float localHeight);

// Restores play->view's ordinary 3D perspective projection (already computed earlier this frame
// by the engine's own View_Apply) and re-enables the Z-buffer geometry mode, so draw calls issued
// after this pass see the normal camera again. Always call after Begin(), even if no models were
// placed.
void Zelda3D_Overlay2D_End(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_OVERLAY2D_H
