// Zelda3D::TitleCloudVortex — port of OoT3D's ACTOR-layer Death Mountain cloud ring at the
// title screen (the bright, churning half of the cloud vortex above the peak).
//
// GROUND TRUTH (oot3d-decomp/docs/title_cloud_vortex.md; derived 2026-07-10 from the embedded-
// Azahar draw log + per-pixel TEV dump at the settled title, az frame 1000):
//
// OoT3D's title vortex is TWO concentric additive ring draws, not one:
//   1. spot99 room mesh material 0 (`doughnut_modelT` 64x64 ETC1A4): MODULATE(PRIMARY, TEX0) x1,
//      blend src=SRC_ALPHA dst=ONE, static. SoH already draws this as room draw-group 24.
//   2. THIS draw — zelda_efc_doughnut.zar's `doughnut_modelT.cmb` (128x128 ETC1A4 ring):
//      TEV stage 0 MODULATE(PRIMARY_COLOR, TEXTURE0) at scaleRGB=x2 (stage 1 is a
//      MODULATE(PREV, CONST4=white) pass-through), blend src=SRC_ALPHA dst=ONE, depth test on /
//      write off, cull off (double-sided), isVertexLighting=1. Oracle per-pixel dump:
//      PRIMARY_COLOR = (60,74,100,255) = sceneAmbient x 2 enabled lights x baked vColor(0.502)
//      with FULL alpha — the same scene-lit feed the room ring gets, so the byte-driven generic
//      pipeline (combScaleRGB, additive blend, ambient sum) already renders it correctly; the
//      only missing piece was the DRAW itself. Without it SoH showed a single x1 ring: about a
//      third of the additive energy and none of the churning arm structure (the arms are the two
//      rings' patterns beating against each other as this one rotates).
//
// PLACEMENT (asset-derived, no magic constants):
//   - anchor  = centroid of the room's own ring mesh (spot99 room 0, material 0): the two rings
//     render concentric in the oracle (screen bboxes x 397-480 match within a few px).
//   - scale   = 0.1: N64 Bg_Spot16_Doughnut's default background scale (z_bg_spot16_doughnut.c
//     Init, `Actor_SetScale(&this->actor, 0.1f)`); byte-consistent — the actor CMB's model-space
//     ring radius is 28800, x0.1 = 2880 = exactly the room ring's world radius.
//   - rotation = shape.rot.y -= 0x20 per frame (same N64 actor's Update law; the 3DS remake
//     reuses this actor's behavior). Verified against the oracle: the swirl pattern visibly
//     rotates over 60/120-frame captures while the ring geometry stays put.
//   - color: N64 draw sets env/prim white, alpha 255 (non-fiery branch) — tint white here.
//
// The zar's third piece (`doughnut_aya_modelT.cmb`, alpha-blended + its own UV-scroll cmab) is
// NOT drawn at the settled title per the oracle draw log (no 64x128 ETC1A4 alpha-blend draw in
// the frame) — deliberately not ported here; documented in the decomp doc for whoever meets it
// in-game (Death Mountain Trail's fiery variant).
#include "title_cloud_vortex.h"
#include "title_activity.h"
#include "../../render/model_group_diagnostics.h"
#include "../../render/model_queries.h"
#include "functions/math.h"
#include "soh/frame_interpolation.h" // OPEN_DISPS/gSPMatrix record hooks (extern "C" decls)

#include <cstdio>

namespace {

// N64 Bg_Spot16_Doughnut background-scene scale (z_bg_spot16_doughnut.c Init default case;
// model radius 28800 x 0.1 == the room ring's 2880 world radius).
constexpr float kRingScale = 0.1f;
// N64 Bg_Spot16_Doughnut_Update: this->actor.shape.rot.y -= 0x20 each frame (binary angle).
constexpr int kRingRotStepBinang = -0x20;

} // namespace

void Zelda3D_TitleCloudVortex_Emit(PlayState* play, int roomModelId) {
    if (!Zelda3D_Title_IsActive()) {
        return;
    }
    Zelda3D_EnsureModelProvider();
    // "SKY:" load prefix = keep the CMB's baked vertex color (the ring's constant 0.502 gray that
    // the oracle's PRIMARY_COLOR measurement requires) — the far-plane sky behavior is a DRAW-time
    // flag (handle bit 30) which this draw does not set, so depth test vs the mountain still applies.
    static int sModelId = Zelda3D_AutoModelId("SKY:/actor/zelda_efc_doughnut.zar|doughnut_modelT");
    // Anchor: the room's own doughnut ring mesh centroid (spot99 room 0 material 0) — the actor
    // ring is concentric with it in the oracle. Resolved once per room model.
    static int sAnchorForModel = -1;
    static float sAnchor[3] = { 0.0f, 0.0f, 0.0f };
    static bool sAnchorOk = false;
    if (sAnchorForModel != roomModelId) {
        sAnchorForModel = roomModelId;
        sAnchorOk = Zelda3D_ModelGroupCentroid(roomModelId, /*materialIndex=*/0, sAnchor) != 0;
        if (!sAnchorOk) {
            std::fprintf(stderr, "[Zelda3D] title cloud vortex: no material-0 ring in room model %d\n", roomModelId);
        }
    }
    if (!sAnchorOk || sModelId < 0) {
        return;
    }

    s16 rotY = (s16)(kRingRotStepBinang * (s32)(play->gameplayFrames & 0xFFFF));

    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Translate(sAnchor[0], sAnchor[1], sAnchor[2], MTXMODE_NEW);
    Matrix_RotateY(BINANG_TO_RAD(rotY), MTXMODE_APPLY);
    Matrix_Scale(kRingScale, kRingScale, kRingScale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPZelda3DDraw(POLY_OPA_DISP++, sModelId, 255, 255, 255);
    CLOSE_DISPS(play->state.gfxCtx);
}
