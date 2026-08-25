// See zelda3d_overlay2d.h for the design rationale.
#include "global.h"
#include "functions/math.h"
#include "functions/rendering.h"
#include "zelda3d_overlay2d.h"

extern "C" void Zelda3D_Overlay2D_Begin(PlayState* play, float refW, float refH) {
    if (play == nullptr || refW <= 0.0f || refH <= 0.0f) {
        return;
    }
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    // Allocated (and the null check resolved) BEFORE OPEN_DISPS: OPEN_DISPS/CLOSE_DISPS expand to
    // a matched, UNNESTABLE {...} pair (see macros.h) — an early return between them (as a naive
    // null-guard here would need) corrupts that brace matching, so any early-out has to happen
    // outside the bracketed region entirely.
    Mtx* ortho = (Mtx*)Graph_Alloc(gfxCtx, sizeof(Mtx));
    if (ortho == nullptr) {
        return;
    }
    // Top-left origin, Y DOWN: bottom=refH maps to clip -1 (screen bottom), top=0 maps to clip +1
    // (screen top) — a pixel-space translate of (x,y) with x in [0,refW], y in [0,refH] lands
    // exactly where it visually reads.
    //
    // near/far are NOT arbitrary (#146 item B): the SG renderer's model pipelines always
    // depth-test (zelda3d_sdl3gpu.cpp getPipeline, LESS_OR_EQUAL), so intra-model depth ordering
    // within this pass is real. PlaceModel's fixed 180° X rotation (kOverlayFixedRotX, the
    // Y-up->Y-down convention flip below) also negates model Z, which INVERTS the authored depth
    // sense: a sub-mesh modeled BEHIND (more negative local z — title_logo_us.cmb's shield at z
    // -6.3..-9.7 vs the ZELDA letters at -5.0..-5.6) came out NEARER, so the shield depth-tested
    // in FRONT of the letters — the occlusion inversion measured in
    // debug_journal/2026-07-10-shield-sword-attribution.md §5. Passing the ortho near/far
    // REVERSED (near=+1000, far=-1000) re-flips clip z so the authored order is restored
    // (modeled-behind maps farther, matching the oracle's own projection). Range stays wide
    // enough that any caller z never clips.
    guOrtho(ortho, 0.0f, refW, refH, 0.0f, 1000.0f, -1000.0f, 1.0f);
    OPEN_DISPS(gfxCtx);
    gSPMatrix(OVERLAY_DISP++, ortho, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    // #146 item B: give this pass its OWN depth scope instead of disabling depth entirely.
    // gSPClearGeometryMode(G_ZBUFFER) below is the legacy Fast3D signal (kept for any interleaved
    // N64 geometry) but has NO effect on the OoT3D CMB models this pass actually draws — those go
    // through the unified Zelda3D SG renderer (gSPZelda3DDrawA -> DrawModel), whose depth test is
    // always on and whose depth WRITE is a static per-material flag baked from the CMB. For
    // title_logo_us.cmb specifically that leaves intra-model ordering (shield/sword vs the "ZELDA"
    // letters — see debug_journal/2026-07-10-shield-sword-attribution.md) to raw draw-call
    // submission order, which is wrong: the model's own vertex depth places the shield/sword
    // BEHIND the letters, but SoH shows it unoccluded.
    //
    // Fix: reset the shared depth buffer to far HERE (a fullscreen depth-only draw, color writes
    // off — Fast::Zelda3DRenderer::ClearOverlayDepth, no render-pass split) so this pass starts
    // with a blank depth canvas. Safe to do unconditionally: the 3D scene behind this overlay has
    // already been fully composited into the COLOR buffer by this point, so wiping depth cannot
    // damage it — it only affects what THIS pass's own models depth-test against, restoring
    // correct self-occlusion within title_logo_us.cmb without needing per-mesh sorting or a
    // depth-write override on the CMB materials themselves (still statically false/true as
    // authored — depth WRITE is still governed by the CMB's own material flags, this fixes the
    // buffer's *starting state*, not per-material write behavior).
    gSPZelda3DClearDepth(OVERLAY_DISP++);
    // No depth test/write against the already-finished 3D scene's Z-buffer — this pass composites
    // purely by draw order (same reasoning z_eff_blure.c/z_eff_spark.c already use G_ZBUFFER
    // clears for their own screen-relative effects).
    gSPClearGeometryMode(OVERLAY_DISP++, G_ZBUFFER);
    CLOSE_DISPS(gfxCtx);
}

// Fixed 180-degree X-axis correction — a CONSTANT, not a per-frame camera-derived value. Earlier
// attempts composed this with the LIVE play->billboardMtxF (the moving title-cs camera's own
// rotation) on the theory that the decompiled actor's "camera-basis technique"
// (oot3d-decomp/docs/title_logo_actor.md §6.1) needed to be preserved; that was tested and
// FALSIFIED — at cf700 (where the camera happened to be near this fixed orientation) it looked
// correct, but at cf1500 (a different camera angle later in the same cutscene) the SAME live
// rotation flipped the wordmark upside-down/mirrored again, proving the live camera basis has no
// valid meaning once the outer camera projection it used to compose against (P*V) is gone — this
// ortho pass has no "V" for a camera-relative rotation to be relative TO.
//
// ROOT CAUSE (traced 2026-07-10, not just empirically fitted): this is a Y-axis CONVENTION flip,
// not a camera-facing correction at all. Confirmed by tracing both sides:
//   - CMB import (cmb3d/cmb.cpp Cmb::readAttr/computeBoneMatrices) applies NO axis flip/swap to
//     vertex positions or bone matrices — a model's local space is exactly what's authored in the
//     CMB file, Y-up (same convention SoH's N64 world already uses).
//   - Every OTHER consumer of that local space in this engine treats it as Y-up: normal 3D actor/
//     room draws (zelda3d.c Zelda3D_EmitModelDraw) apply only the actor's own rot/scale, no baked
//     axis correction; SoH's own PRE-EXISTING 2D ortho primitive (z_view.c View_ApplyOrtho) builds
//     its guOrtho box as guOrtho(proj, -w/2, w/2, -h/2, h/2, ...) — a standard CENTERED, Y-UP box
//     (larger Y = toward screen top), matching CMB/world space directly with no per-model flip.
//   - Zelda3D_Overlay2D_Begin (below), by contrast, DELIBERATELY builds its guOrtho box the other
//     way: guOrtho(ortho, 0, refW, refH, 0, ...) — bottom=refH, top=0, i.e. a top-left-origin,
//     Y-DOWN pixel box (Y increases going DOWN the screen), chosen so every placement fraction in
//     title_logo.cpp/title_fireglow.cpp (measured directly off oracle screenshots, which are
//     naturally Y-down pixel coordinates) converts to this pass's space with a single multiply, no
//     unit/axis conversion at the call site — and so the primitive stays ergonomic for its stated
//     future consumers (file-select/HUD overlays, which think in screen pixels, not centered
//     world-style Y-up units).
// That deliberate Y-down choice is exactly what a Y-up-authored CMB model needs corrected for: a
// pure X-axis 180 rotation maps local (x,y,z) -> (x,-y,-z), turning "local Y-up" into "local
// Y-down", which is what this Y-down box expects. The accompanying Z-sign flip is inert here (no
// compensating culling/winding fix needed): Begin() disables the Z-buffer geometry mode entirely
// (compositing is pure draw-call order in this pass), and there is no depth test for a flipped
// winding to interact with.
//
// Proper home: THIS is the correct location for the fix, not Begin()'s guOrtho call. Flipping
// Begin() to a Y-up box instead would remove the need for this per-model constant, but would
// require re-deriving every existing placement fraction in title_logo.cpp/title_fireglow.cpp
// (measured as Y-down pixel offsets from oracle screenshots) into centered Y-up units, AND would
// make the primitive less ergonomic for the HUD/file-select consumers this module's header
// explicitly anticipates (screen-space pixel placement, not centered world units) — regressing the
// primitive's own design goal to remove one now-well-understood constant. Keeping the flip here,
// applied uniformly to every model Zelda3D_Overlay2D_PlaceModel places, is the generic, correct
// fix: every future 2D-overlay consumer of this pass inherits it automatically.
static constexpr float kOverlayFixedRotX = 3.14159265f;

extern "C" void Zelda3D_Overlay2D_PlaceModel(PlayState* play, float cxPx, float cyPx, float heightPx,
                                              float localHeight) {
    if (play == nullptr || localHeight <= 0.0f) {
        return;
    }
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    const float scale = heightPx / localHeight;
    OPEN_DISPS(gfxCtx);
    Matrix_Translate(cxPx, cyPx, 0.0f, MTXMODE_NEW);
    Matrix_RotateX(kOverlayFixedRotX, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(OVERLAY_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    CLOSE_DISPS(gfxCtx);
}

extern "C" void Zelda3D_Overlay2D_End(PlayState* play) {
    if (play == nullptr) {
        return;
    }
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    gSPSetGeometryMode(OVERLAY_DISP++, G_ZBUFFER);
    if (play->view.projectionPtr != nullptr) {
        gSPMatrix(OVERLAY_DISP++, play->view.projectionPtr, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    }
    CLOSE_DISPS(gfxCtx);
}
