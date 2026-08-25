// Zelda3D behavior: En_Horse (Epona) — the OoT3D render-gap port surface.
//
// En_Horse is a Skin-system actor (z_skin.c), NOT a SkelAnime one, and Zelda3D hooks only the
// SkelAnime draw family — so Epona's BODY currently ALWAYS draws as the native N64 mesh (with the HD
// texture pack); the AUTO-skinned classification `actorsnear` reports is a static eligibility label,
// not a live per-frame draw confirmation. Wiring a Zelda3D choke point into Skin_DrawImpl is a larger,
// separate port (the honest render-gap remainder — oot3d-decomp/docs/en_horse_epona_render_gap.md).
//
// What DOES need OoT3D-faithful handling now are two DRAW-ADJACENT behaviors: the N64 twin derives
// each from the native skin pose / N64 collision mesh, but under the Zelda3D render they must follow
// the ON-SCREEN state instead. Both were RE'd + verified in-session (2026-07-15) and previously landed
// as pokes scattered across zelda3d.c + zelda3d_render.cpp; consolidated here per the per-actor-module
// rule (the enhorse.module-port RE-frontier step).
//
//   1. Hoof-dust spawn Y  — Zelda3D_HoofDustWorldPos. z_en_horse.c's EnHorse_PostDraw stamps a hoof
//      dust position via Skin_GetLimbPos, then spawns EffectSsDust there the next frame. That native Y
//      comes off the N64 collision mesh, which lacks the hill relief the OoT3D-warped RENDER terrain
//      has — so the dust punches through / floats above the ground. Reconcile the hoof's Y onto the
//      render terrain at the hoof's OWN XZ (so a laterally-offset hoof lands under itself, not under
//      the horse's center). Same class of fix as the title tree-grounding commit (36525326), applied
//      to the dust effect. oot3d-decomp/docs/en_horse_hoof_dust.md,
//      debug_journal/2026-07-15-epona-hoof-dust-depth.md.
//
//   2. Rider seat offset  — Zelda3D_HorseSaddleOffset (#152). GROUND TRUTH
//      (oot3d-decomp/docs/en_horse_rider_pos.md): OoT3D's EnHorse_Update (FUN_0014a5a8) computes
//      riderPos as the actor-relative offset of posed JOINT 14 via FUN_00408828 — the rig's dedicated
//      zero-geometry rider-attach bone (parent = torso, local trans (1268,-1764,0); the N64
//      {600,-1670,0} riderOffset constant does not exist in code.bin — Grezzo baked it into the
//      skeleton as this bone). The N64 EnHorse_PostDraw instead derives riderPos from the N64 Skin
//      pose (limb 30) — but under the Zelda3D replacement the horse VISIBLY plays a 3DS CSAB whose
//      pose can diverge entirely from the N64 skelAnime's (title rear: N64 side idles while the 3DS
//      clip rears -> Link buried in the neck). So anchor the seat to the on-screen pose exactly as the
//      3DS does: posed bone-14 origin lifted through the transform Zelda3D_EmitModelDraw applied this
//      frame (T(pos)·R_YXZ(shape.rot)·S(worldScale)·T(0,groundOff,0)) minus the actor position — the
//      same actor-relative convention riderPos already uses (and the same FUN_00408828 performs).
#include "global.h" // Vec3f and the N64 transform types used at this port boundary
#include "functions/math.h"
#include "en_horse.h"
#include "zelda3d/anim/pose_tracking.h"
#include "zelda3d/render/terrain_alignment_render.h"

// #152 rider seat: last replaced-draw transform recorded for the (single) live EnHorse. Populated by
// Zelda3D_EmitModelDraw via Zelda3D_EnHorse_RecordDraw; read by Zelda3D_HorseSaddleOffset.
static Actor* sHorseDrawActor = nullptr;
static int    sHorseDrawModel = -1;

// Run-scoped reset (Zelda3D_CoreRunBegin): the actor belongs to the previous run's heap, and the
// model id is only meaningful paired with it.
extern "C" void Zelda3D_HorseRefsResetRunState(void) {
    sHorseDrawActor = nullptr;
    sHorseDrawModel = -1;
}
static float  sHorseDrawScale = 0.0f;
static float  sHorseDrawGroundOff = 0.0f;

extern "C" void Zelda3D_EnHorse_RecordDraw(Actor* actor, int modelId, float worldScale, float groundOffset) {
    sHorseDrawActor = actor;
    sHorseDrawModel = modelId;
    sHorseDrawScale = worldScale;
    sHorseDrawGroundOff = groundOffset;
    if (actor != nullptr && modelId >= 0) {
        // The posed-skin cache Zelda3D_PosedBoneWorldPos reads is only maintained while min-Y tracking
        // is on (same enable the selected-actor framing path uses).
        Zelda3D_SetTrackPosedMinY(modelId, 1);
    }
}

// (1) Hoof-dust Y reconciliation — see file header. Given a hoof's ALREADY-COMPUTED native world
// position (ioPos, from z_en_horse.c's Skin_GetLimbPos), lift/drop its Y onto the OoT3D-warped render
// terrain at the hoof's own XZ. Modifies ioPos[1] in place; returns 1 if a correction was applied, 0
// if none was needed/available (terrain warp inactive, no OoT3D room mesh — ioPos left unchanged).
extern "C" int Zelda3D_HoofDustWorldPos(PlayState* play, Actor* horseActor, float* ioPos) {
    if (play == nullptr || horseActor == nullptr || ioPos == nullptr) {
        return 0;
    }
    float dy = Zelda3D_RenderYOffsetAtXZ(play, horseActor, ioPos[0], ioPos[2]);
    if (dy == 0.0f) {
        return 0;
    }
    ioPos[1] += dy;
    return 1;
}

// (2) Rider seat offset — see file header. Returns 0 when the horse isn't the Zelda3D-drawn EnHorse
// (caller falls back to the native N64 Skin_GetLimbPos path).
extern "C" int Zelda3D_HorseSaddleOffset(Actor* horse, float out[3]) {
    if (horse == nullptr || out == nullptr || horse != sHorseDrawActor || sHorseDrawModel < 0) {
        return 0;
    }
    float local[3];
    if (!Zelda3D_PosedBoneWorldPos(sHorseDrawModel, 14, local)) {
        return 0;
    }
    Matrix_Push();
    Matrix_Translate(0.0f, 0.0f, 0.0f, MTXMODE_NEW);
    Matrix_RotateY(BINANG_TO_RAD(horse->shape.rot.y), MTXMODE_APPLY);
    Matrix_RotateX(BINANG_TO_RAD(horse->shape.rot.x), MTXMODE_APPLY);
    Matrix_RotateZ(BINANG_TO_RAD(horse->shape.rot.z), MTXMODE_APPLY);
    Matrix_Scale(sHorseDrawScale, sHorseDrawScale, sHorseDrawScale, MTXMODE_APPLY);
    Vec3f l = { local[0], local[1] + sHorseDrawGroundOff, local[2] };
    Vec3f w;
    Matrix_MultVec3f(&l, &w);
    Matrix_Pop();
    out[0] = w.x;
    out[1] = w.y;
    out[2] = w.z;
    return 1;
}
