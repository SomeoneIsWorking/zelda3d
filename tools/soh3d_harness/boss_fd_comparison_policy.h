#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_COMPARISON_POLICY_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_COMPARISON_POLICY_H

#include <array>

#include "boss_fd_compare.h"
#include "boss_fd_oracle.h"
#include "soh_boss_fd_state.h"

namespace HarnessBossFdComparison {

inline constexpr double kPositionTolerance = static_cast<double>(0.05F);
inline constexpr double kRotationTolerance = static_cast<double>(0.001F);
inline constexpr double kSpeedTolerance = static_cast<double>(0.01F);
inline constexpr double kTurnRateTolerance = static_cast<double>(0.05F);
inline constexpr double kRingSelfPositionTolerance = static_cast<double>(0.001F);
inline constexpr double kRingSelfRotationTolerance = static_cast<double>(0.00001F);

enum class Reason {
    Match,
    InvalidSnapshot,
    OutsideForcedProfile,
    UnpairedState,
    InsufficientHistory,
    LeadCursorMismatch,
    ToleranceExceeded,
};

struct Result {
    BossFdCompareStatus status = BossFdCompareStatus::Invalid;
    Reason reason = Reason::InvalidSnapshot;
    double producerPositionDelta = 0.0;
    double producerRotationDelta = 0.0;
    double producerVelocityDelta = 0.0;
    double producerSpeedDelta = 0.0;
    double producerTurnDelta = 0.0;
    double positionMean = 0.0;
    double positionMax = 0.0;
    double rotationMean = 0.0;
    double rotationMax = 0.0;
    double oracleSelfPositionDelta = 0.0;
    double oracleSelfRotationDelta = 0.0;
    double sohSelfPositionDelta = 0.0;
    double sohSelfRotationDelta = 0.0;
};

using History = std::array<float, BOSS_FD_HISTORY_COUNT * 3>;

Result Evaluate(const HarnessBossFdOracle::State& oracle, const BossFdAuthoredState& authored,
                const BossFdNativeInputs& native, int sohScene, int sohHistoryCount, const History& sohPosition,
                const History& sohRotation);
const char* ReasonName(Reason reason);

} // namespace HarnessBossFdComparison

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_COMPARISON_POLICY_H
