#include "boss_fd_profile_validation.h"

#include <cmath>

#include "../../Shipwright/soh/src/zelda3d/behaviors/actor/boss_fd/forced_flight_profile.h"

namespace HarnessBossFdProfile {
namespace {

using namespace Zelda3D::BossFdForcedProfile;

bool Within(float actual, float expected, float tolerance) {
    return std::abs(actual - expected) <= tolerance;
}

bool ControlsMatch(const HarnessBossFdOracle::State& oracle, const BossFdNativeInputs& native, float tolerance) {
    if (oracle.action != kAction || native.action != kAction) {
        return false;
    }
    const float target[] = { kTargetX, kTargetY, kTargetZ };
    for (int axis = 0; axis < 3; ++axis) {
        if (!Within(oracle.target[axis], target[axis], tolerance) ||
            !Within(native.targetPosition[axis], target[axis], tolerance)) {
            return false;
        }
    }
    const float expected[] = { kSpeed, kTurnRate, kTurnRateMax, kWobbleAmplitude, kWobbleRate };
    const float actual[] = { native.flySpeed, native.turnRate, native.turnRateMax, native.flyWobbleAmplitude,
                             native.flyWobbleRate };
    for (int field = 1; field < 5; ++field) {
        if (!Within(oracle.controls[field], expected[field], tolerance) ||
            !Within(actual[field], expected[field], tolerance)) {
            return false;
        }
    }
    // Fly-speed control: the ORACLE slot is rewritten by its own action to the pool constant each
    // tick, and our AUTHORED producer applies the same constant before consuming it. The raw soh
    // fwork slot is NOT compared here: the N64-path overlay overwrites it after every authored
    // pre-update, so it never reflects what the authored integration consumed.
    return Within(oracle.controls[0], kSpeed, tolerance);
}

} // namespace

bool MatchesComparisonScope(const HarnessBossFdOracle::State& oracle, const BossFdNativeInputs& native,
                            const BossFdAuthoredState& authored, float tolerance) {
    return tolerance >= 0.0F && ControlsMatch(oracle, native, tolerance) &&
           Within(authored.appliedFlySpeedControl, kSpeed, tolerance);
}

bool MatchesForcedInitialization(const HarnessBossFdOracle::State& oracle, const BossFdNativeInputs& native) {
    if (!ControlsMatch(oracle, native, 0.0F) || oracle.startAttack != 0 || native.startAttack != 0 ||
        oracle.stopFlag != 0 || native.stopFlag != 0 || oracle.introState != 0 || native.introState != 0 ||
        oracle.speed != kSpeed || native.speed != kSpeed ||
        // Init-time only: the raw fwork slot still holds the forced value because no host frame
        // has run since the write. Post-warm scope checks use the authored applied control instead.
        native.flySpeed != kSpeed ||
        oracle.actionFunction != HarnessBossFdOracle::kFlightActionFunction || oracle.moveTimer != kMoveTimer ||
        native.moveTimer != kMoveTimer || oracle.actionTimer != kActionTimer || native.actionTimer != kActionTimer) {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (oracle.displacement[axis] != 0.0F || native.displacement[axis] != 0.0F) {
            return false;
        }
    }
    return true;
}

} // namespace HarnessBossFdProfile
