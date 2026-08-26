#include <cassert>
#include <cmath>
#include <limits>

#include "boss_fd_comparison_policy.h"
#include "boss_fd_profile_validation.h"
#include "zelda3d/behaviors/actor/boss_fd/forced_flight_profile.h"

namespace {

using namespace Zelda3D::BossFdForcedProfile;

struct Fixture {
    HarnessBossFdOracle::State oracle{};
    BossFdAuthoredState authored{};
    BossFdNativeInputs native{};
    HarnessBossFdComparison::History position{};
    HarnessBossFdComparison::History rotation{};

    Fixture() {
        oracle.scene = 1;
        oracle.action = kAction;
        oracle.moveTimer = 9;
        oracle.bodyLead = 0;
        oracle.speed = kSpeed;
        oracle.target = { kTargetX, kTargetY, kTargetZ };
        oracle.controls = { kSpeed, kTurnRate, kTurnRateMax, kWobbleAmplitude, kWobbleRate };

        authored.bodyLead = 0;
        authored.sampleCount = BOSS_FD_HISTORY_COUNT;
        authored.authoredMoveTimer = oracle.moveTimer;
        authored.visualSpeed = kSpeed;
        authored.visualTurnRate = kTurnRate;
        authored.appliedFlySpeedControl = kSpeed;

        native.action = kAction;
        native.moveTimer = oracle.moveTimer;
        native.targetPosition[0] = kTargetX;
        native.targetPosition[1] = kTargetY;
        native.targetPosition[2] = kTargetZ;
        native.flySpeed = kSpeed;
        native.turnRate = kTurnRate;
        native.turnRateMax = kTurnRateMax;
        native.flyWobbleAmplitude = kWobbleAmplitude;
        native.flyWobbleRate = kWobbleRate;
    }

    HarnessBossFdComparison::Result evaluate() const {
        return HarnessBossFdComparison::Evaluate(oracle, authored, native, oracle.scene, BOSS_FD_HISTORY_COUNT,
                                                 position, rotation);
    }
};

} // namespace

int main() {
    Fixture exact;
    assert(exact.evaluate().status == BossFdCompareStatus::Match);

    Fixture boundary;
    boundary.position[141 * 3] = static_cast<float>(HarnessBossFdComparison::kPositionTolerance);
    assert(boundary.evaluate().status == BossFdCompareStatus::Match);

    Fixture divergence;
    divergence.position[141 * 3] = static_cast<float>(HarnessBossFdComparison::kPositionTolerance + 0.001);
    assert(divergence.evaluate().status == BossFdCompareStatus::Diverged);

    Fixture warming;
    warming.authored.sampleCount = 1;
    warming.authored.visualPos[0] = 3.0F;
    const auto warmingResult = warming.evaluate();
    assert(warmingResult.status == BossFdCompareStatus::Missing);
    assert(warmingResult.producerPositionDelta == 3.0);

    Fixture wrappedRotation;
    constexpr float kPi = 3.14159265358979323846F;
    wrappedRotation.oracle.historyRot[141 * 3] = kPi - 0.0002F;
    wrappedRotation.rotation[141 * 3] = -kPi + 0.0002F;
    assert(wrappedRotation.evaluate().status == BossFdCompareStatus::Match);

    Fixture invalid;
    invalid.position[1] = std::numeric_limits<float>::quiet_NaN();
    assert(invalid.evaluate().reason == HarnessBossFdComparison::Reason::InvalidSnapshot);

    Fixture profileTolerance;
    profileTolerance.native.targetPosition[0] = kTargetX + 0.0005F;
    assert(HarnessBossFdProfile::MatchesComparisonScope(profileTolerance.oracle, profileTolerance.native,
                                                        profileTolerance.authored, 0.001F));
    assert(!HarnessBossFdProfile::MatchesComparisonScope(profileTolerance.oracle, profileTolerance.native,
                                                         profileTolerance.authored, 0.0001F));
    return 0;
}
