// See title_rider.h for the relocation rationale. step()'s integrator body moved verbatim from
// zelda3d.c's Zelda3D_RiderStepCue; applyToActor()/releaseMount() are the new horse-attribution
// port (title_rider.h's header comment, oot3d-decomp/docs/title_rider_port_spec.md).
#include "global.h"
#include "title_rider.h"
#include "../../zelda3d_cutscene.h"
#include "overlays/actors/ovl_En_Horse/z_en_horse.h"

extern "C" {
// Shared math primitives (RE'd + verified vs Az; zelda3d.c) — see title_rider.h for why these
// stay there instead of moving here (still used by zelda3d.c's dead Zelda3D_RiderStep path).
int16_t Zelda3D_ActorTurnToPoint(int16_t cur_yaw, float dx, float dz, int32_t max_step);
void Zelda3D_PathFollowUpdate(float pos[3], int16_t* yaw, float* speed_xz, const int32_t waypoint[3]);
void Zelda3D_ActorMoveXZByYawSpeed(float pos[3], int16_t yaw, float speed_xz);
// EnHorse state-transition helpers (z_en_horse.c) — file-scope-visible (not static) but not
// declared in z_en_horse.h; forward-declared here the same way zelda3d.c exposes other
// "was static, now shared" primitives (see e.g. Zelda3D_TitleCamEnabled in title_presentation.cpp).
void EnHorse_MountedGallopReset(EnHorse* horse);
void EnHorse_MountedTrotReset(EnHorse* horse);
void EnHorse_StartMountedIdleResetAnim(EnHorse* horse);
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
        case 0x24: return 1; // CsMoveToPoint      (FUN_003cf3c4)
        case 0x25: return 2; // CsJump             (FUN_001033d4; unused by the title cs)
        case 0x26: return 3; // CsRearing          (FUN_0010360c; unused by the title cs)
        case 0x40: return 4; // CsWarpMoveToPoint  (FUN_00230d84)
        case 0x41: return 5; // CsWarpRearing      (FUN_002535f0)
        default:   return 0;
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
        const bool teleport = (mCsFuncIdx == 0) ||
                              (funcIdx == 4) ||  // WarpMoveInit      (FUN_002a8af8) teleports
                              (funcIdx == 5);    // CsWarpRearingInit (FUN_002b6c00) teleports
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
            // EnHorse_CsJump -> EnHorse_CsMoveToPoint); the title cs never authors 0x25, so the
            // full parabolic-jump body (FUN_003ab99c family) is deliberately not ported here.
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
            mHorseActor = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_HORSE,
                                       mPos[0], mPos[1], mPos[2], 0, mYaw, 0, 1);
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
    horse->actor.shape.rot.y = horse->actor.world.rot.y = mYaw;
    horse->actor.velocity.x = horse->actor.velocity.y = horse->actor.velocity.z = 0.0f;

    // Force-select the cued gait (port spec step 4) EVERY frame, not just on the cue-action
    // transition: EnHorse's stock action funcs (EnHorse_MountedGallop/-Trot/-MountedIdle) read
    // REAL controller stick input to decide whether to hold/demote the gait, which the title
    // attract-demo has none of — left alone, the horse would decay to a standstill within a few
    // frames of the cue-driven ride starting. Re-asserting `action`/`speedXZ` after the horse's
    // own Update ran this frame (Zelda3D_ActorPostUpdate fires post-update, per-actor) means the
    // NEXT frame's sActionFuncs[this->action] dispatch (z_en_horse.c's EnHorse_Update) always lands
    // back on the cued gait, and that function's OWN SkelAnime_Update keeps the animation
    // advancing normally — the CSAB auto-resolver then picks up the right clip off the horse's
    // live N64 animation resource with zero title-specific anim code (zelda3d_animmap.inc's
    // existing object_horse table, unchanged).
    EnHorseAction wantAction;
    s32 wantAnimIdx;
    f32 wantSpeed;
    switch (mCueAction) {
        case 0x41: // CsWarpRearing — rearing-into-idle in place. Approximated with the mounted
                   // idle gait for now (no rearing Reset helper is exposed by z_en_horse.c);
                   // trajectory-correct (speed 0), anim divergence noted in the 2026-07-14
                   // rider-cs-dispatch journal as a follow-up.
        case 0x26: // CsRearing — same approximation
            wantAction = ENHORSE_ACT_MOUNTED_IDLE;
            wantAnimIdx = ENHORSE_ANIM_IDLE;
            wantSpeed = 0.0f;
            break;
        case 0x24: // CsMove — GALLOP anim, decomp-confirmed: CsMoveInit (FUN_0016ca48) selects
                   // anim slot 7, the same slot WarpMoveInit (FUN_002a8af8) uses for its gallop
                   // default; matches N64 EnHorse_CsMoveInit's ENHORSE_ANIM_GALLOP. (The earlier
                   // trot mapping was a guess and made Epona trot at 8 u/frame.)
        case 0x40: // CsWarpMove — gallop
        default:
            wantAction = ENHORSE_ACT_MOUNTED_GALLOP;
            wantAnimIdx = ENHORSE_ANIM_GALLOP;
            wantSpeed = 8.0f;
            break;
    }
    if (horse->action != wantAction) {
        // Prime the new clip once on a transition (Animation_PlayOnce/-Change inside these Reset
        // helpers — the same ones z_en_horse.c's own gait-switch call sites use), rather than every
        // frame, so playback isn't restarted every tick.
        if (wantAction == ENHORSE_ACT_MOUNTED_GALLOP) {
            EnHorse_MountedGallopReset(horse);
        } else if (wantAction == ENHORSE_ACT_MOUNTED_TROT) {
            EnHorse_MountedTrotReset(horse);
        } else {
            EnHorse_StartMountedIdleResetAnim(horse);
        }
    }
    horse->action = wantAction;
    horse->animationIdx = wantAnimIdx;
    horse->actor.speedXZ = wantSpeed;
    // Stop EnHorse_MountDismount's one-shot mount-edge Freeze transition (z_en_horse.c) from
    // re-firing every frame — it only fires while playerControlled==false, and its target
    // (ENHORSE_ACT_FROZEN) would otherwise fight the gait forced above.
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
