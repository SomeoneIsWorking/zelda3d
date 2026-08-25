// See title_rider.h for the relocation rationale. step()'s integrator body moved verbatim from
// zelda3d.c's Zelda3D_RiderStepCue; applyToActor()/releaseMount() are the new horse-attribution
// port (title_rider.h's header comment, oot3d-decomp/docs/title_rider_port_spec.md).
#include "global.h"
#include "functions/actors.h"
#include "functions/collision.h"
#include <cmath>
#include "title_rider.h"
#include "../../core/zelda3d_log.h"
#include "../../cutscene/zelda3d_cutscene.h"
#include "overlays/actors/ovl_En_Horse/z_en_horse.h"

extern "C" {
// Shared math primitives (RE'd + verified vs Az; zelda3d.c) — see title_rider.h for why these
// stay there instead of moving here (still used by zelda3d.c's dead Zelda3D_RiderStep path).
int16_t Zelda3D_ActorTurnToPoint(int16_t cur_yaw, float dx, float dz, int32_t max_step);
void Zelda3D_PathFollowUpdate(float pos[3], int16_t* yaw, float* speed_xz, const int32_t waypoint[3]);
void Zelda3D_ActorMoveXZByYawSpeed(float pos[3], int16_t yaw, float speed_xz);
// EnHorse state-transition helpers (z_en_horse.c) — file-scope-visible (not static) but not
// declared in z_en_horse.h; forward-declared here the same way zelda3d.c exposes other
// "was static, now shared" primitives (see e.g. Zelda3D_TitleCamEnabled in title_activity.cpp).
void EnHorse_MountedGallopReset(EnHorse* horse);
void EnHorse_MountedTrotReset(EnHorse* horse);
void EnHorse_StartMountedIdleResetAnim(EnHorse* horse);
// EnHorse's own cs-action init/action funcs (z_en_horse.c ~2240-2355) — the vendored N64
// EnHorse_CutsceneUpdate's sCutsceneInitFuncs[3]/[5] and sCutsceneActionFuncs[3]/[5], confirmed
// 1:1 structurally with the 3DS FUN_002b6c00 (idx5 init: teleport + animationIdx=ENHORSE_ANIM_
// REARING, ANIMMODE_ONCE via Animation_Change) and FUN_002535f0 (idx5 action: speedXZ=0 every
// frame; on SkelAnime_Update completion, animationIdx -> ENHORSE_ANIM_IDLE) per
// oot3d-decomp/docs/title_rider_cs_dispatch.md's 2026-07-14 addendum. Reused verbatim below
// instead of the old ENHORSE_ACT_MOUNTED_IDLE approximation (no anim-select hack needed — these
// functions already ARE the correct anim-select+SFX+end-behavior logic).
void EnHorse_CsRearingInit(EnHorse* horse, PlayState* play, CsCmdActorCue* cue);
void EnHorse_CsRearing(EnHorse* horse, PlayState* play, CsCmdActorCue* cue);
void EnHorse_CsWarpRearingInit(EnHorse* horse, PlayState* play, CsCmdActorCue* cue);
void EnHorse_CsWarpRearing(EnHorse* horse, PlayState* play, CsCmdActorCue* cue);
// EnHorse_CsMoveToPoint/EnHorse_CsWarpMoveToPoint's animation-only tail (z_en_horse.c) — see that
// file's comment for why the position-mutating halves of those functions are NOT called here.
void EnHorse_CsMoveAnimOnly(EnHorse* horse);
void EnHorse_CsMoveInitAnimOnly(EnHorse* horse);
// Player's native ongoing-mounted-ride action func (z_player.c ~13825) and the generic standing
// idle func (~8492) reused verbatim so Link's rider pose/anim selection (mount) and post-title
// fallback pose (release) fall out of the same code real gameplay uses (port spec step 3). Neither
// is declared in a header; forward-declared per the same pattern.
void Player_Action_8084CC98(Player* player, PlayState* play);
void Player_Action_Idle(Player* player, PlayState* play);
}

namespace Zelda3D {

namespace {

// Cue action id -> cs-function index. Literal port of the pair table at .data 0x00526dfc
// (byte-identical to N64 z_en_horse.c sCsActionTable) and the inlined
// EnHorse_GetCutsceneFunctionIndex lookup in FUN_0026a30c: exact match returns the paired index,
// anything else returns 0 (hold). See title_rider.h's step() doc for the derivation trail.
int RiderCsFuncIdx(uint16_t action) {
    switch (action) {
        case 0x24:
            return 1; // CsMoveToPoint      (FUN_003cf3c4)
        case 0x25:
            return 2; // CsJump             (FUN_001033d4; unused by the title cs)
        case 0x26:
            return 3; // CsRearing          (FUN_0010360c; unused by the title cs)
        case 0x40:
            return 4; // CsWarpMoveToPoint  (FUN_00230d84)
        case 0x41:
            return 5; // CsWarpRearing      (FUN_002535f0)
        default:
            return 0;
    }
}

} // namespace

void TitleRider::step(PlayState* play, int csFrame, bool* outDiscontinuity) {
    if (outDiscontinuity != nullptr) {
        *outDiscontinuity = false;
    }
    int cueIdx, startF, endF;
    int16_t cueYaw;
    float p0[3], p1[3];
    uint16_t cueAction = mCueAction;
    if (!Zelda3D_TitleCsRiderCue(csFrame, &cueIdx, p0, p1, &startF, &endF, &cueYaw, &cueAction)) {
        return; // no cue latched this frame — hold pose (3DS dispatcher: NULL channel ptr -> return)
    }
    mCueAction = cueAction;

    const int funcIdx = RiderCsFuncIdx(cueAction);
    if (funcIdx == 0) {
        return; // unknown action id — 3DS dispatcher holds (uVar5 == 0 branch)
    }
    if (funcIdx != mCsFuncIdx) {
        // First-ever cue: the dispatcher body itself seeds the transform from the cue before
        // running the init func (FUN_0026a30c's csAction==0 branch).
        const bool teleport = (mCsFuncIdx == 0) || (funcIdx == 4) || // WarpMoveInit      (FUN_002a8af8) teleports
                              (funcIdx == 5);                        // CsWarpRearingInit (FUN_002b6c00) teleports
        mCsFuncIdx = funcIdx;
        if (teleport) {
            mPos[0] = p0[0];
            mPos[1] = p0[1];
            mPos[2] = p0[2];
            mYaw = cueYaw; // cue rot[1], exactly what both warp inits store to world.rot.y
            if (outDiscontinuity != nullptr) {
                *outDiscontinuity = true;
            }
        }
        // CsMoveInit / CsJumpInit / CsRearingInit do not touch the transform (anim-only inits;
        // gait selection lives in applyToActor's cue-action mapping).
    }

    // Per-frame action func.
    switch (mCsFuncIdx) {
        case 3:
        case 5:
            // CsRearing / CsWarpRearing: speed_xz = 0 every frame, no movement (FUN_002535f0's
            // first store; the rest of those bodies is anim/sfx).
            mSpeed = 0.0f;
            break;
        case 2:
            // CsJump falls through to the move integrator when its jump flag isn't armed (N64
            // EnHorse_CsJump -> EnHorse_CsMoveToPoint); the title cs never authors 0x25 (jump),
            // so no jump body is needed here AT ALL. RE'd 2026-07-16
            // (scratch/decomp_agent/rider_jump/): FUN_003ab99c is NOT a title-cs function — it is
            // the Gerudo Valley bridge jump (N64 EnHorse_BridgeJumpMove family, byte-exact
            // constants + the sBridgeJumps table at 0x00526d68), dispatched from EnHorse's
            // gameplay action table; and the cs dispatch table's own 0x25 slot
            // (FUN_00190A20/FUN_001033D4) is a third, separate jump implementation that the
            // title script never invokes.
        case 1:
        case 4: {
            // CsMoveToPoint / CsWarpMoveToPoint — byte-identical integrator bodies (FUN_003cf3c4 /
            // FUN_00230d84): 3D dist to the cue's p1; <= 8.0 snaps + speed 0, else turn toward it
            // capped at 267 binang and set speed 8.0; then Actor_MoveXZByYawSpeed integrates.
            const int32_t wp[3] = { (int32_t)p1[0], (int32_t)p1[1], (int32_t)p1[2] };
            Zelda3D_PathFollowUpdate(mPos, &mYaw, &mSpeed, wp);
            Zelda3D_ActorMoveXZByYawSpeed(mPos, mYaw, mSpeed);
            break;
        }
        default:
            break;
    }

    // Y: Az's rider follows the terrain (Epona walks the ground); raycast SoH's floor at the
    // integrated XZ. Fall back to the cue-endpoint Y (i.e. leave mPos[1] as PathFollowUpdate/
    // ActorMoveXZByYawSpeed left it) when there's no floor.
    {
        Vec3f q = { mPos[0], mPos[1] + 200.0f, mPos[2] };
        CollisionPoly poly;
        f32 y = BgCheck_AnyRaycastFloor1(&play->colCtx, &poly, &q);
        if (y > BGCHECK_Y_MIN + 1.0f) {
            mPos[1] = y;
        }
    }
}

void TitleRider::applyToActor(PlayState* play, Actor* actor) {
    if (play == nullptr || actor == nullptr) {
        return;
    }
    if (actor->id == ACTOR_PLAYER) {
        Player* player = (Player*)actor;
        if (mHorseActor == nullptr) {
            // Spawn the title-scoped Epona once per title entry, at the rider cue's current
            // pos/yaw. params=1 -> HORSE_EPONA, the same "plain ridable Epona" params z_horse.c's
            // own spawn sites use (e.g. z_horse.c:76) — EnHorse_Init's early Actor_Kill branches
            // all gate on SCENE_LON_LON_RANCH/SCENE_STABLE/SCENE_GERUDOS_FORTRESS, none of which is
            // the title's SCENE_TITLE, so the spawn always survives init here.
            mHorseActor = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_HORSE, mPos[0], mPos[1], mPos[2], 0, mYaw, 0, 1);
            if (mHorseActor != nullptr) {
                // Mount Link onto her via the literal native field-set + call z_player.c's own
                // A-press mount path uses (~7169-7193), minus the button-press/put-away/climb-on
                // transient — the oracle demo begins already mid-ride, not mid-mount, so
                // actionVar2 is seeded straight to Player_Action_8084CC98's "riding" branch
                // (>=1, see that function's header comment) instead of 0 (which would play the
                // multi-frame get-on-horse sub-animation the oracle never shows).
                player->rideActor = mHorseActor;
                player->stateFlags1 |= PLAYER_STATE1_ON_HORSE;
                player->actor.parent = mHorseActor;
                Actor_MountHorse(play, player, mHorseActor);
                player->actionFunc = Player_Action_8084CC98;
                player->av2.actionVar2 = 99;
                mPlayerActor = &player->actor;
            }
        }
        return; // Link's transform now derives from the horse every frame via his own actionFunc
    }
    if (actor->id != ACTOR_EN_HORSE || actor != mHorseActor) {
        return;
    }

    EnHorse* horse = (EnHorse*)actor;
    horse->actor.world.pos.x = mPos[0];
    horse->actor.world.pos.y = mPos[1];
    horse->actor.world.pos.z = mPos[2];
    // 60fps sub-frame: mPos integrates once per cs tick (30fps); on the hold engine frame render
    // half a step ahead along the heading so the rider moves EVERY engine frame instead of
    // stepping at half rate (kanban #149; pairs with the camera's fractional spline eval in
    // title_rider_state.cpp). Move cues only — rearing/idle have speed 0, and teleport frames
    // land on the advance tick (subframe 0), so no discontinuity is smeared.
    {
        const float sub = Zelda3D_TitleCsSubframe();
        if (sub > 0.0f && mSpeed > 0.0f) {
            const int fi = RiderCsFuncIdx(mCueAction);
            if (fi == 1 || fi == 4) {
                const float yawRad = (float)mYaw * (float)M_PI / 32768.0f;
                horse->actor.world.pos.x += sinf(yawRad) * mSpeed * sub;
                horse->actor.world.pos.z += cosf(yawRad) * mSpeed * sub;
                // Y follows the terrain (step()'s raycast) but only at cs cadence — re-raycast at
                // the advanced XZ so the ground-follow is also per-engine-frame (the measured
                // ~2.7-unit 30Hz vertical stepping on the uphill gallop, kanban #149).
                Vec3f q = { horse->actor.world.pos.x, horse->actor.world.pos.y + 200.0f, horse->actor.world.pos.z };
                CollisionPoly poly;
                f32 y = BgCheck_AnyRaycastFloor1(&play->colCtx, &poly, &q);
                if (y > BGCHECK_Y_MIN + 1.0f) {
                    horse->actor.world.pos.y = y;
                }
            }
        }
    }
    horse->actor.shape.rot.y = horse->actor.world.rot.y = mYaw;
    horse->actor.velocity.x = horse->actor.velocity.y = horse->actor.velocity.z = 0.0f;

    // Rearing (cue 0x41/0x26): drive EnHorse's OWN cs-action init/action funcs (z_en_horse.c
    // EnHorse_CsWarpRearingInit/-CsWarpRearing, EnHorse_CsRearingInit/-CsRearing) instead of
    // approximating with the mounted-idle gait. These are the literal vendored N64 dispatch-table
    // entries EnHorse_CutsceneUpdate would call for this cue action (sCutsceneInitFuncs[3]/[5],
    // sCutsceneActionFuncs[3]/[5]) and are structurally 1:1 with the decompiled 3DS FUN_002b6c00/
    // FUN_002535f0 (oot3d-decomp/docs/title_rider_cs_dispatch.md) — same teleport-on-init +
    // ANIMMODE_ONCE rearing clip + speedXZ=0 + auto-transition to ENHORSE_ANIM_IDLE when the once-
    // clip finishes. `horse->cutsceneAction` (the native field these functions themselves use,
    // z_en_horse.h +0x380) doubles as the transition-edge flag here exactly as it does inside the
    // real EnHorse_CutsceneUpdate — the title path just supplies the cue instead of csCtx.linkAction.
    const int funcIdx = RiderCsFuncIdx(mCueAction);
    if (funcIdx == 3 || funcIdx == 5) {
        // `log rider 1`: transition diagnosis (see the Move branch's twin below).
        Z3D_LOG(RIDER,
                "REARING funcIdx=%d cutsceneAction(before)=%d animIdx(before)=%d action=%d "
                "animFrame=%.2f riderPos=(%.1f,%.1f,%.1f) playerPos=(%.1f,%.1f,%.1f)\n",
                funcIdx, (int)horse->cutsceneAction, (int)horse->animationIdx, (int)horse->action,
                horse->skin.skelAnime.curFrame, horse->riderPos.x, horse->riderPos.y, horse->riderPos.z,
                mPlayerActor ? mPlayerActor->world.pos.x : 0.0f, mPlayerActor ? mPlayerActor->world.pos.y : 0.0f,
                mPlayerActor ? mPlayerActor->world.pos.z : 0.0f);
        CsCmdActorCue cue{};
        cue.action = mCueAction;
        // Only the *Init funcs read startPos/urot.y (teleport target); at the exact transition
        // frame mPos/mYaw already equal the cue's p0/cueYaw (TitleRider::step() teleported them
        // there this same frame, before applyToActor runs) — see step()'s teleport branch.
        cue.startPos.x = (s32)mPos[0];
        cue.startPos.y = (s32)mPos[1];
        cue.startPos.z = (s32)mPos[2];
        cue.urot.y = (uint16_t)mYaw;
        if (horse->cutsceneAction != funcIdx) {
            horse->cutsceneAction = funcIdx;
            if (funcIdx == 5) {
                EnHorse_CsWarpRearingInit(horse, play, &cue);
            } else {
                EnHorse_CsRearingInit(horse, play, &cue);
            }
        }
        if (funcIdx == 5) {
            EnHorse_CsWarpRearing(horse, play, &cue);
        } else {
            EnHorse_CsRearing(horse, play, &cue);
        }
        // horse->action/animationIdx/speedXZ are now authoritatively owned by the Cs func just
        // called (including its own end-of-clip -> ENHORSE_ANIM_IDLE transition). Park the horse's
        // own next-frame dispatch on ENHORSE_ACT_CS_UPDATE: its action func (EnHorse_CutsceneUpdate)
        // returns immediately while play->csCtx.linkAction is NULL (which it always is at title —
        // the title cs runs on Zelda3D's own interpreter, not the N64 csCtx), so THIS post-update
        // call is the horse's sole animation driver — the same single-dispatcher structure as the
        // 3DS FUN_0026a30c. Anything else (e.g. ENHORSE_ACT_MOUNTED_REARING) would make
        // EnHorse_Update run a SECOND stick-driven action func each frame, double-stepping
        // SkelAnime_Update (2x playback) and fighting the end-transition.
        horse->action = ENHORSE_ACT_CS_UPDATE;
        horse->playerControlled = 1;
        horse->cyl1.base.ocFlags1 |= OC1_ON;
        horse->cyl2.base.ocFlags1 |= OC1_ON;
        horse->jntSph.base.ocFlags1 |= OC1_ON;
        return;
    }

    // Move / WarpMove (cue 0x24/0x40, funcIdx 1/4): drive the SkelAnime purely through
    // EnHorse_CsMoveAnimOnly (z_en_horse.c) — the animation-only tail of the vendored N64
    // EnHorse_CsMoveToPoint/EnHorse_CsWarpMoveToPoint (structurally 1:1 with the decompiled 3DS
    // FUN_003cf3c4/FUN_00230d84, oot3d-decomp/docs/title_rider_cs_dispatch.md), instead of
    // approximating with the gameplay EnHorse_MountedGallop action func.
    //
    // 2026-07-15 divergence hunt: EnHorse_MountedGallop (the previous approximation here) reads
    // REAL controller stick input every frame via EnHorse_UpdateSpeed/EnHorse_StickDirection and
    // gates on EnHorse_PlayerCanMove — none of which the title cs's own native code path
    // exercises (its per-frame func is a constant-speed integrator with NO stick coupling). With
    // zero real input, EnHorse_UpdateSpeed's minStickMag branch decremented speedXZ by 0.06/frame
    // before this post-update hook reset it back to 8.0, so the gallop clip's playSpeed
    // (speedXZ*0.3) was computed from a stale, slightly-decayed speed every single frame instead
    // of the title cs's own steady 8.0. Ruled OUT as the dominant symptom (0016cb18/003cf4f0
    // pool-read confirmed the title cs's own per-frame multiplier is 0.45, not the 0.3 lifted
    // from gameplay — but 0.3*8/24 == 0.45*8/36 (N64 gEponaGallopingAnim's 24 frames vs the
    // 3DS's hl_anim_fastrun2_30's 36-frame duration, oot3d-decomp/docs/csab_catalog.md, both
    // 0.1 cycles/logic-tick) so the PHASE-LOCK path's fraction-of-cycle math already compensates
    // for the differing constants — verified by direct computation, not by guessing). This port
    // still removes the gameplay function's unverified stick/PlayerCanMove coupling and the small
    // per-frame speed jitter it caused, matching the real cs dispatcher exactly instead of
    // approximating it. See debug_journal/2026-07-15-epona-title-animation.md.
    if (funcIdx == 1 || funcIdx == 4) {
        // `log rider 1`: catch who stomps animationIdx between our per-frame calls (the mounted-Link
        // stand-pose diagnosis: Player reads idx=REARING while this branch sets GALLOP).
        Z3D_LOG(RIDER,
                "MOVE funcIdx=%d cutsceneAction(before)=%d animIdx(before)=%d action=%d "
                "pos=(%.1f,%.1f,%.1f) animFrame=%.2f\n",
                funcIdx, (int)horse->cutsceneAction, (int)horse->animationIdx, (int)horse->action,
                horse->actor.world.pos.x, horse->actor.world.pos.y, horse->actor.world.pos.z,
                horse->skin.skelAnime.curFrame);
        if (horse->cutsceneAction != funcIdx) {
            horse->cutsceneAction = funcIdx;
            // WarpMoveInit teleports (this->actor.world.pos = cue startPos) — mPos/mYaw already
            // equal the cue's p0/cueYaw on this exact transition frame (step()'s teleport branch,
            // same pattern as the rearing branch above), so this is a same-value re-write, not a
            // second independent move.
            if (funcIdx == 4) {
                horse->actor.world.pos.x = mPos[0];
                horse->actor.world.pos.y = mPos[1];
                horse->actor.world.pos.z = mPos[2];
                horse->actor.prevPos = horse->actor.world.pos;
                horse->actor.world.rot.y = (int16_t)mYaw;
                horse->actor.shape.rot = horse->actor.world.rot;
            }
            EnHorse_CsMoveInitAnimOnly(horse);
        }
        horse->actor.speedXZ = mSpeed; // step() already integrated the verified trajectory speed
        EnHorse_CsMoveAnimOnly(horse);
        horse->action = ENHORSE_ACT_CS_UPDATE; // sole animation driver this frame, same as rearing
        horse->playerControlled = 1;
        horse->cyl1.base.ocFlags1 |= OC1_ON;
        horse->cyl2.base.ocFlags1 |= OC1_ON;
        horse->jntSph.base.ocFlags1 |= OC1_ON;
        return;
    }

    // Unmapped cue action (funcIdx==0, e.g. before the first cue latches): hold a plain mounted
    // idle rather than freezing mid-clip.
    if (horse->action != ENHORSE_ACT_MOUNTED_IDLE) {
        EnHorse_StartMountedIdleResetAnim(horse);
    }
    horse->action = ENHORSE_ACT_MOUNTED_IDLE;
    horse->animationIdx = ENHORSE_ANIM_IDLE;
    horse->actor.speedXZ = 0.0f;
    horse->cutsceneAction = funcIdx;
    horse->playerControlled = 1;
    horse->cyl1.base.ocFlags1 |= OC1_ON;
    horse->cyl2.base.ocFlags1 |= OC1_ON;
    horse->jntSph.base.ocFlags1 |= OC1_ON;
}

void TitleRider::releaseMount(PlayState* play) {
    (void)play;
    if (mPlayerActor != nullptr) {
        Player* player = (Player*)mPlayerActor;
        player->rideActor = nullptr;
        player->stateFlags1 &= ~PLAYER_STATE1_ON_HORSE;
        player->actor.parent = nullptr;
        // Repoint off the mounted-ride action func before it can run again with rideActor==NULL
        // (Player_Action_8084CC98 dereferences it unconditionally) — e.g. if title is re-entered
        // without a full scene/Player reload (backing out of file select). Player_Action_Idle is
        // the same generic standing-idle func Player_Init's own default path uses.
        player->actionFunc = Player_Action_Idle;
        player->av2.actionVar2 = 0;
        mPlayerActor = nullptr;
    }
    if (mHorseActor != nullptr) {
        // Kill, not just unmount — nothing in the title path should leave a stray EN_HORSE actor
        // lingering into gameplay/attract paths after title hands off.
        Actor_Kill(mHorseActor);
        mHorseActor = nullptr;
    }
    mCsFuncIdx = 0; // next title entry re-seeds from its first cue (dispatcher csAction==0 branch)
}

} // namespace Zelda3D
