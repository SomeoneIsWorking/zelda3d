#include "boss_fd_comparison_policy.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>

#include "../../Shipwright/soh/src/zelda3d/behaviors/actor/boss_fd/history_layout.h"
#include "boss_fd_profile_validation.h"

namespace HarnessBossFdComparison {
namespace {

constexpr float kForcedProfileTolerance = 0.001F;

bool IsFinite(const float* values, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        if (!std::isfinite(values[index])) {
            return false;
        }
    }
    return true;
}

double VectorDistance(const float* left, const float* right) {
    const double x = static_cast<double>(left[0]) - right[0];
    const double y = static_cast<double>(left[1]) - right[1];
    const double z = static_cast<double>(left[2]) - right[2];
    return std::sqrt(x * x + y * y + z * z);
}

double RotationDistance(const float* left, const float* right) {
    constexpr double kTau = 6.28318530717958647692;
    double squared = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        const double delta = std::remainder(static_cast<double>(left[axis]) - right[axis], kTau);
        squared += delta * delta;
    }
    return std::sqrt(squared);
}

Result Finish(BossFdCompareStatus status, Reason reason) {
    Result result;
    result.status = status;
    result.reason = reason;
    return result;
}

} // namespace

Result Evaluate(const HarnessBossFdOracle::State& oracle, const BossFdAuthoredState& authored,
                const BossFdNativeInputs& native, int sohScene, int sohHistoryCount, const History& sohPosition,
                const History& sohRotation) {
    if (sohHistoryCount != BOSS_FD_HISTORY_COUNT || authored.bodyLead < 0 ||
        authored.bodyLead >= BOSS_FD_HISTORY_COUNT || authored.sampleCount < 0 ||
        authored.sampleCount > BOSS_FD_HISTORY_COUNT || oracle.bodyLead < 0 ||
        oracle.bodyLead >= BOSS_FD_HISTORY_COUNT || !IsFinite(sohPosition.data(), sohPosition.size()) ||
        !IsFinite(sohRotation.data(), sohRotation.size()) || !IsFinite(authored.visualPos, 3) ||
        !IsFinite(authored.visualVelocity, 3) || !IsFinite(oracle.velocity.data(), oracle.velocity.size()) ||
        !std::isfinite(authored.visualSpeed) || !std::isfinite(authored.visualTurnRate)) {
        return Finish(BossFdCompareStatus::Invalid, Reason::InvalidSnapshot);
    }
    const float nativeValues[] = { native.targetPosition[0],  native.targetPosition[1], native.targetPosition[2],
                                   native.flySpeed,           native.turnRate,          native.turnRateMax,
                                   native.flyWobbleAmplitude, native.flyWobbleRate };
    if (!IsFinite(nativeValues, std::size(nativeValues))) {
        return Finish(BossFdCompareStatus::Invalid, Reason::InvalidSnapshot);
    }
    if (!HarnessBossFdProfile::MatchesComparisonScope(oracle, native, authored, kForcedProfileTolerance)) {
        return Finish(BossFdCompareStatus::Invalid, Reason::OutsideForcedProfile);
    }
    if (sohScene != oracle.scene || native.action != oracle.action || authored.authoredMoveTimer != oracle.moveTimer) {
        return Finish(BossFdCompareStatus::Invalid, Reason::UnpairedState);
    }
    Result result;
    constexpr float kBinangToRad = 3.14159265358979323846F / 32768.0F;
    const float authoredRotation[] = { authored.visualRot[0] * kBinangToRad, authored.visualRot[1] * kBinangToRad,
                                       authored.visualRot[2] * kBinangToRad };
    const float oracleRotation[] = { oracle.worldRot[0] * kBinangToRad, oracle.worldRot[1] * kBinangToRad,
                                     oracle.worldRot[2] * kBinangToRad };
    result.producerPositionDelta = VectorDistance(oracle.worldPos.data(), authored.visualPos);
    result.producerRotationDelta = RotationDistance(oracleRotation, authoredRotation);
    result.producerVelocityDelta = VectorDistance(oracle.velocity.data(), authored.visualVelocity);
    result.producerSpeedDelta = std::abs(static_cast<double>(authored.visualSpeed) - oracle.speed);
    result.producerTurnDelta = std::abs(static_cast<double>(authored.visualTurnRate) - oracle.controls[1]);
    if (authored.sampleCount < BOSS_FD_HISTORY_COUNT) {
        result.status = BossFdCompareStatus::Missing;
        result.reason = Reason::InsufficientHistory;
        return result;
    }

    for (int offset : Zelda3D::BossFdHistoryLayout::kBodyOffset) {
        const int oracleIndex = (oracle.bodyLead + offset) % BOSS_FD_HISTORY_COUNT;
        const int sohIndex = (authored.bodyLead + offset) % BOSS_FD_HISTORY_COUNT;
        const double positionDelta = VectorDistance(&oracle.historyPos[oracleIndex * 3], &sohPosition[sohIndex * 3]);
        const double rotationDelta = RotationDistance(&oracle.historyRot[oracleIndex * 3], &sohRotation[sohIndex * 3]);
        result.positionMean += positionDelta;
        result.rotationMean += rotationDelta;
        result.positionMax = std::max(result.positionMax, positionDelta);
        result.rotationMax = std::max(result.rotationMax, rotationDelta);
    }
    const double samples = Zelda3D::BossFdHistoryLayout::kBodyOffset.size();
    result.positionMean /= samples;
    result.rotationMean /= samples;

    result.oracleSelfPositionDelta = VectorDistance(&oracle.historyPos[oracle.bodyLead * 3], oracle.worldPos.data());
    result.oracleSelfRotationDelta = RotationDistance(&oracle.historyRot[oracle.bodyLead * 3], oracleRotation);
    result.sohSelfPositionDelta = VectorDistance(&sohPosition[authored.bodyLead * 3], authored.visualPos);
    result.sohSelfRotationDelta = RotationDistance(&sohRotation[authored.bodyLead * 3], authoredRotation);
    if (result.oracleSelfPositionDelta > kRingSelfPositionTolerance ||
        result.oracleSelfRotationDelta > kRingSelfRotationTolerance ||
        result.sohSelfPositionDelta > kRingSelfPositionTolerance ||
        result.sohSelfRotationDelta > kRingSelfRotationTolerance) {
        result.status = BossFdCompareStatus::Invalid;
        result.reason = Reason::LeadCursorMismatch;
        return result;
    }

    const bool matches =
        result.producerPositionDelta <= kPositionTolerance && result.producerRotationDelta <= kRotationTolerance &&
        result.producerSpeedDelta <= kSpeedTolerance && result.producerTurnDelta <= kTurnRateTolerance &&
        result.positionMax <= kPositionTolerance && result.rotationMax <= kRotationTolerance;
    result.status = matches ? BossFdCompareStatus::Match : BossFdCompareStatus::Diverged;
    result.reason = matches ? Reason::Match : Reason::ToleranceExceeded;
    return result;
}

const char* ReasonName(Reason reason) {
    switch (reason) {
        case Reason::Match:
            return "match";
        case Reason::InvalidSnapshot:
            return "invalid-snapshot";
        case Reason::OutsideForcedProfile:
            return "outside-forced-profile";
        case Reason::UnpairedState:
            return "unpaired-state";
        case Reason::InsufficientHistory:
            return "insufficient-history";
        case Reason::LeadCursorMismatch:
            return "lead-cursor-mismatch";
        case Reason::ToleranceExceeded:
            return "tolerance-exceeded";
    }
    return "invalid-snapshot";
}

} // namespace HarnessBossFdComparison
