// SoH3D behavior: En_Butte (ambient butterfly) — animated model REPLACEMENT.
//
// Ground truth: N64 EnButte_Draw draws a 3-limb SkelAnime butterfly (gButterflySkel + gButterflyAnim,
// OBJECT_GAMEPLAY_FIELD_KEEP) via SkelAnime_DrawSkeletonOpa, gated on `drawSkelAnime`. OoT3D keeps the
// same butterfly in the equivalent keep zar, `/actor/zelda_field_keep.zar`:
//   model  Model/butterfly.cmb     — 3 bones (body + 2 wings), confirmed by enumerating the zar
//   anim   Anim/butterfly_fly.csab — the looping wing-flap / flight clip
// One CMB + one CSAB for every butterfly in the game, so it drops into the standard prop transform
// (world.pos + shape.rot) with the wing flap driven by the CSAB. The butterfly's FLIGHT (wander,
// hover, follow-Link, fairy transform) is shared N64 code that runs unchanged and just moves
// actor.world.pos + shape.rot, which this draw follows; only the wing-flap DRAW is replaced.
//
// The CSAB is phase-locked to the actor's live N64 SkelAnime (SoH3D_UpdateAnimAuto), so each butterfly
// flaps at its own anim tempo — multiple butterflies share one GL model id but the per-emit pose
// snapshot (SoH3D_GL_EmitPose) pairs each draw with its own pose, so they animate independently.
//
// Fairy transform: when Link plays Song of Storms nearby the butterfly turns into a fairy — N64 sets
// drawSkelAnime=false and draws a flash effect (EnButte_DrawTransformationEffect, gEffFlash1DL)
// instead of the skeleton. We honor that by falling through to the N64 draw whenever drawSkelAnime is
// false, so the transform effect renders unchanged.
#include "z64.h"
#include "src/overlays/actors/ovl_En_Butte/z_en_butte.h" // EnButte (skelAnime + drawSkelAnime, 64-bit-safe)
#include "en_butte.h"

// Field-keep zar + the butterfly CMB. The CSAB (butterfly_fly) resolves from the same zar by name.
#define SOH3D_BUTTE_ZAR "/actor/zelda_field_keep.zar"
#define SOH3D_BUTTE_CMB "Model/butterfly.cmb"
#define SOH3D_BUTTE_CSAB "butterfly_fly"

// World scale: N64 draws gButterflyDL at actor scale 0.01 (ICHAIN_VEC3F_DIV1000(scale,10)); the OoT3D
// CMB is ~735 units wide. Calibrated live (Kakariko entrance 0xDB) to 0.006 to match the small N64
// butterfly footprint (0.02 was ~3x too large). Live-retunable via REPL `gscale 23`.
static constexpr float kButteWorldScale = 0.006f;
static constexpr int kButteGScaleSlot = 23;
// Free-run fallback rate (frames/draw) when the N64 anim has no lockable progress; phase-lock is used
// whenever the live N64 anim length is meaningful (it is for the butterfly's looping flight clip).
static constexpr float kButteAnimRate = 1.0f;

extern "C" {
int SoH3D_AutoModelId(const char* zarPath);
int SoH3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
float SoH3D_GScale(int slot, float def);
// CSAB driver (phase-lock to the live N64 anim / free-run + morph), keyed per model id; sets the
// model's GPU skinning pose for this draw. See SoH3D_UpdateAnimAuto in soh3d_anim.cpp.
void SoH3D_UpdateAnimAuto(int modelId, const char* animName, float rate, float n64CurFrame,
                          float n64AnimLength, float morphWeight);
}

namespace SoH3D {

s16 EnButteBehavior::actorId() const {
    return ACTOR_EN_BUTTE;
}

bool EnButteBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    EnButte* b = reinterpret_cast<EnButte*>(actor);
    // During the fairy transform the butterfly draws a flash effect, not the skeleton — let N64 draw it.
    if (!b->drawSkelAnime) {
        return false;
    }
    static int sModelId = 0; // 0 = unresolved, <0 = no CMB (fall through to N64)
    if (sModelId == 0) {
        sModelId = SoH3D_AutoModelId(SOH3D_BUTTE_ZAR "|" SOH3D_BUTTE_CMB);
    }
    if (sModelId < 0) {
        return false; // no OoT3D butterfly CMB -> let the N64 butterfly draw
    }
    // Flap the wings via butterfly_fly.csab, phase-locked to the live N64 SkelAnime so the tempo
    // matches; the per-emit pose snapshot pairs this actor's pose with its own draw.
    SoH3D_UpdateAnimAuto(sModelId, SOH3D_BUTTE_CSAB, kButteAnimRate, b->skelAnime.curFrame,
                         b->skelAnime.animLength, b->skelAnime.morphWeight);
    SoH3D_DrawActorModel(play, sModelId, actor, SoH3D_GScale(kButteGScaleSlot, kButteWorldScale));
    return true;
}

} // namespace SoH3D
