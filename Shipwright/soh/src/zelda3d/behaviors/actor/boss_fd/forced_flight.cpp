// Reproducible constant-control profile for paired Boss_Fd producer verification.
#include "authored_flight.h"
#include "functions/actors.h"
#include "forced_flight.h"
#include "forced_flight_profile.h"

#include "overlays/actors/ovl_Boss_Fd/z_boss_fd.h"

extern "C" void BossFd_SetupFly(BossFd* boss, PlayState* play);

static_assert(Zelda3D::BossFdForcedProfile::kAction == BOSSFD_FLY_MAIN);

extern "C" int Zelda3D_BossFdForceFly(Actor* actor) {
    return Zelda3D_BossFdForceFlySeeded(actor, nullptr, nullptr);
}

extern "C" int Zelda3D_BossFdForceFlySeeded(Actor* actor, const float* pos3, const short* rot3) {
    using namespace Zelda3D::BossFdForcedProfile;
    if (!actor || actor->id != ACTOR_BOSS_FD)
        return 0;
    BossFd* boss = reinterpret_cast<BossFd*>(actor);
    if (pos3 != nullptr && rot3 != nullptr) {
        // Seed-lock: the paired oracle is written to the same world transform before its own
        // force, so both producers start from identical initial conditions. The flight is
        // chaotic enough that even a 3-degree seed difference decorrelates the orbits within
        // the comparison window; equal dynamics are then observable as zero divergence.
        boss->actor.world.pos = { pos3[0], pos3[1], pos3[2] };
        boss->actor.world.rot = { rot3[0], rot3[1], rot3[2] };
        boss->actor.shape.rot = boss->actor.world.rot;
    }
    BossFd_SetupFly(boss, nullptr);
    boss->introState = BFD_CS_NONE;
    boss->work[BFD_ACTION_STATE] = kAction;
    boss->work[BFD_MOVE_TIMER] = kMoveTimer;
    boss->work[BFD_STOP_FLAG] = false;
    boss->work[BFD_START_ATTACK] = false;
    boss->timers[0] = kActionTimer;
    boss->targetPosition = { kTargetX, kTargetY, kTargetZ };
    boss->actor.speedXZ = boss->fwork[BFD_FLY_SPEED] = kSpeed;
    boss->fwork[BFD_TURN_RATE] = kTurnRate;
    boss->fwork[BFD_TURN_RATE_MAX] = kTurnRateMax;
    boss->fwork[BFD_FLY_WOBBLE_AMP] = kWobbleAmplitude;
    boss->fwork[BFD_FLY_WOBBLE_RATE] = kWobbleRate;
    boss->actor.colChkInfo.displacement = {};
    Actor_UpdateVelocityXYZ(&boss->actor);
    Zelda3D::BossFdFlight::reset(actor);
    return 1;
}
