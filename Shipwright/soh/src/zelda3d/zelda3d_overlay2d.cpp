// See zelda3d_overlay2d.h for the design rationale.
#include "global.h"
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
    // exactly where it visually reads. near/far are arbitrary (no real depth in this space; all
    // ordering is draw-call order with the Z-buffer disabled below), picked wide enough that any
    // z a caller might pass never clips.
    guOrtho(ortho, 0.0f, refW, refH, 0.0f, -1000.0f, 1000.0f, 1.0f);
    OPEN_DISPS(gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, ortho, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    // No depth test/write against the already-finished 3D scene's Z-buffer — this pass composites
    // purely by draw order (same reasoning z_eff_blure.c/z_eff_spark.c already use G_ZBUFFER
    // clears for their own screen-relative effects).
    gSPClearGeometryMode(POLY_OPA_DISP++, G_ZBUFFER);
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
// ortho pass has no "V" for a camera-relative rotation to be relative TO. The correct, decomp-
// compatible reading of §6.1 is: OoT3D's mechanism needs SOME camera basis to define "facing the
// viewer" for a genuinely moving 3D camera; our ortho pass has a FIXED, non-moving virtual camera
// by construction, so the equivalent fixed basis is this single constant, applied identically every
// frame regardless of the title-cs camera's own motion — which is exactly what makes the placement
// camera-independent (the actual goal of this port).
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
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    CLOSE_DISPS(gfxCtx);
}

extern "C" void Zelda3D_Overlay2D_End(PlayState* play) {
    if (play == nullptr) {
        return;
    }
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_ZBUFFER);
    if (play->view.projectionPtr != nullptr) {
        gSPMatrix(POLY_OPA_DISP++, play->view.projectionPtr, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    }
    CLOSE_DISPS(gfxCtx);
}
