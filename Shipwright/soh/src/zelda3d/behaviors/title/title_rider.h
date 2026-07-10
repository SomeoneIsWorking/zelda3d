// Zelda3D title-demo cue-driven rider (Link+Epona) integrator.
//
// Ported verbatim out of zelda3d.c's Zelda3D_RiderStepCue as part of the title-presentation
// module consolidation (debug_journal/2026-07-08-oot3d-title-module-design.md, migration table
// row "Zelda3D_RiderStepCue() + Zelda3D_ActorTurnToPoint/Zelda3D_PathFollowUpdate/
// Zelda3D_ActorMoveXZByYawSpeed"). This is a pure relocation: the integration math and cue
// semantics are UNCHANGED, only the storage (file-scope statics -> class members) and call site
// (TitlePresentation::update(), via zelda3d.c's Zelda3D_Title_Update bridge) moved.
//
// DEVIATION from the design doc's literal migration table: the three low-level math primitives
// (Zelda3D_ActorTurnToPoint / Zelda3D_PathFollowUpdate / Zelda3D_ActorMoveXZByYawSpeed) stay in
// zelda3d.c as shared (now non-static) C functions rather than moving into this file. Reason:
// they are also called by the older, currently-DEAD waypoint-path Zelda3D_RiderStep (unreferenced
// since the cue-driven path superseded it, task #12) which still lives in zelda3d.c; moving the
// primitives here would either strand that dead function (compile break) or require deleting it,
// and deleting dead code is explicitly step 8 of the migration order (deferred, out of scope for
// this relocation pass). Keeping them as shared primitives the rider component CALLS mirrors how
// Zelda3D_TryDrawSky/TryDrawSunMoon stay shared and are merely called by the title module (design
// doc §5).
//
// HORSE ATTRIBUTION (2026-07-10, oot3d-decomp/docs/title_rider_port_spec.md): the oracle's rider
// is Epona (a real EN_HORSE actor the cue track drives) with Link mounted on her via the native
// gameplay mount mechanism — NOT Link's bare Player actor teleported along the path (that was the
// pre-existing SoH3D gap: debug_journal/2026-07-10-oracle-horse-attribution.md). TitleRider now
// OWNS the title-scoped EN_HORSE instance: spawns it once per title entry (Actor_Spawn, the same
// call z_horse.c's own spawn sites use), mounts Link onto it via the literal
// z_player.c ~7158-7193 field set + Actor_MountHorse(), and force-drives the horse's native
// mounted-gallop/trot/idle action funcs (sActionFuncs[this->action] in z_en_horse.c) each frame so
// its own SkelAnime_Update keeps animating — the CSAB auto-resolver (zelda3d_animmap.inc) then
// picks the right clip off the horse's live N64 animation exactly as it would for any other
// gameplay ride, with zero title-specific CSAB code. See applyToActor()'s doc comment for why
// forcing the `action`/`speedXZ` fields every frame (rather than one-shot at mount time) is
// necessary — EnHorse's stock AI reads real controller input, which the title demo has none of.
#ifndef ZELDA3D_BEHAVIORS_TITLE_TITLE_RIDER_H
#define ZELDA3D_BEHAVIORS_TITLE_TITLE_RIDER_H

#include "global.h"

namespace Zelda3D {

class TitleRider {
public:
    // Integrate one frame from the title cs actor cues (op-0x0a records). No-op (holds pose) if
    // the cs has no cue covering `csFrame`. `outDiscontinuity`, if non-null, is set true the exact
    // frame the rider teleports on a shot-cut cue-chain break (new: not read by anything yet — a
    // seam for the future rider-mount/teleport-not-lerp fix, task #8; the underlying teleport
    // DECISION itself is unchanged from the original Zelda3D_RiderStepCue).
    void step(PlayState* play, int csFrame, bool* outDiscontinuity);

    const float* pos() const { return mPos; }
    int16_t yaw() const { return mYaw; }

    // Per-actor apply, called from Zelda3D_ActorPostUpdate (zelda3d.c) for EVERY actor while the
    // title demo is active — mirrors the call shape Zelda3D_ActorPostUpdate already uses for every
    // other per-actor override in that file (linkpin, boss_goma climb hold, etc). No-op for actors
    // that are neither the title's Player nor its (possibly not-yet-spawned) EN_HORSE instance.
    //
    // - ACTOR_PLAYER: on the first call after title becomes active (mHorseActor == nullptr),
    //   spawns the title EN_HORSE instance at the rider cue's current pos/yaw and mounts Link onto
    //   it (port spec step 2). Does NOT write Link's world.pos/rot directly (spec step 3 — that
    //   now falls out of Link's native mounted-rider action func, Player_Action_8084CC98, reading
    //   the horse's transform every frame on its own).
    // - the spawned ACTOR_EN_HORSE instance: applies this frame's cue-driven world.pos/rot.y (spec
    //   step 1, retargeted from the old ACTOR_PLAYER branch) and re-asserts the native
    //   mounted-gallop/trot/idle action state (spec step 4) — see .cpp for why this must run every
    //   frame rather than once.
    void applyToActor(PlayState* play, Actor* actor);

    // Title-exit teardown (called once from TitlePresentation::exit()): un-mounts Link
    // symmetrically (clears rideActor/PLAYER_STATE1_ON_HORSE/actor.parent) and kills the spawned
    // EN_HORSE instance so nothing lingers into gameplay/attract paths after title hands off.
    void releaseMount(PlayState* play);

private:
    // Initial values verbatim from zelda3d.c's gZelda3dRiderPos/gZelda3dRiderYaw initializers
    // (see that file's history for the RE trail: Az's 0x005AFFB0 read at the shot-1 anchor).
    float mPos[3]  = { -5898.0f, 59.8f, 5091.6f };
    int16_t mYaw   = 0x2AAA;
    float mSpeed   = 8.0f;
    int mCueIdx    = -1;
    uint16_t mCueAction = 0x40; // last-seen RiderCue::action (default gallop; see step())

    Actor* mHorseActor  = nullptr; // the title-scoped EN_HORSE instance, or null before/after mount
    Actor* mPlayerActor = nullptr; // the mounted Player actor, kept for symmetric releaseMount()
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_RIDER_H
