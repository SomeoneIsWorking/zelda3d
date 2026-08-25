#include "boss_fd_compare.h"

#include <cstdio>

#include "boss_fd_comparison_policy.h"
#include "boss_fd_oracle.h"
#include "core/core.h"
#include "core/memory.h"
#include "soh_boss_fd_state.h"
#include "soh_play_state.h"

namespace {

BossFdCompareStatus gLastCompareStatus = BossFdCompareStatus::Invalid;

using HarnessBossFdOracle::Lookup;
using HarnessBossFdOracle::LookupStatus;
using HarnessBossFdOracle::State;

void PrintNativeInputs(const State& oracle, const BossFdNativeInputs& native, const BossFdAuthoredState& authored) {
    std::printf("    native-inputs oracle action=%d move=%d target=(%.2f,%.2f,%.2f) "
                "desiredSpeed/turn/max/wobble/rate=(%.3f,%.3f,%.3f,%.3f,%.3f)\n",
                oracle.action, oracle.moveTimer, oracle.target[0], oracle.target[1], oracle.target[2],
                oracle.controls[0], oracle.controls[1], oracle.controls[2], oracle.controls[3], oracle.controls[4]);
    std::printf("    native-inputs soh action=%d move=%d target=(%.2f,%.2f,%.2f) "
                "desiredSpeed/turn/max/wobble/rate=(%.3f,%.3f,%.3f,%.3f,%.3f) appliedSpeed=%.3f\n",
                native.action, native.moveTimer, native.targetPosition[0], native.targetPosition[1],
                native.targetPosition[2], native.flySpeed, native.turnRate, native.turnRateMax,
                native.flyWobbleAmplitude, native.flyWobbleRate, authored.appliedFlySpeedControl);
}

BossFdCompareStatus RecordStatus(BossFdCompareStatus status) {
    gLastCompareStatus = status;
    return status;
}

} // namespace

BossFdCompareStatus LastBossFdCompareStatus() {
    return gLastCompareStatus;
}

const char* BossFdCompareStatusName(BossFdCompareStatus status) {
    switch (status) {
        case BossFdCompareStatus::Match:
            return "MATCH";
        case BossFdCompareStatus::Diverged:
            return "DIVERGED";
        case BossFdCompareStatus::Missing:
            return "MISSING";
        case BossFdCompareStatus::Invalid:
            return "INVALID";
    }
    return "INVALID";
}

BossFdCompareStatus CompareBossFd(uint32_t azPlayState) {
    auto& memory = Core::System::GetInstance().Memory();
    const Lookup oracleLookup =
        azPlayState == 0 ? Lookup{ LookupStatus::Missing, 0 } : HarnessBossFdOracle::Find(memory, azPlayState);

    HarnessBossFdComparison::History sohPosition{};
    HarnessBossFdComparison::History sohRotation{};
    BossFdAuthoredState authored{};
    BossFdNativeInputs native{};
    const int historyCount =
        SohState_BossFdAuthoredState(&authored, sohPosition.data(), sohRotation.data(), BOSS_FD_HISTORY_COUNT);
    const bool nativeFound = SohState_BossFdNativeInputs(&native) != 0;

    if (oracleLookup.status == LookupStatus::Invalid) {
        std::printf("  bossfd verdict=INVALID reason=oracle-actor-list-read\n");
        return RecordStatus(BossFdCompareStatus::Invalid);
    }
    if (oracleLookup.status == LookupStatus::Missing || historyCount == 0 || !nativeFound) {
        std::printf("  bossfd verdict=MISSING oracle=%s soh-authored=%s soh-native=%s\n",
                    oracleLookup.status == LookupStatus::Found ? "found" : "missing",
                    historyCount ? "found" : "missing", nativeFound ? "found" : "missing");
        return RecordStatus(BossFdCompareStatus::Missing);
    }

    State oracle{};
    if (!HarnessBossFdOracle::Read(memory, azPlayState, oracleLookup.address, &oracle)) {
        std::printf("  bossfd verdict=INVALID reason=oracle-state-read\n");
        return RecordStatus(BossFdCompareStatus::Invalid);
    }

    const int sohScene = SohState_SceneNum();
    const HarnessBossFdComparison::Result result =
        HarnessBossFdComparison::Evaluate(oracle, authored, native, sohScene, historyCount, sohPosition, sohRotation);
    std::printf("  bossfd state oracleAddr=0x%08x scene=0x%04x action=%d lead=%d "
                "sohScene=0x%04x action=%d lead=%d samples=%d\n",
                oracle.address, oracle.scene, oracle.action, oracle.bodyLead, sohScene & 0xFFFF, native.action,
                authored.bodyLead, authored.sampleCount);
    PrintNativeInputs(oracle, native, authored);
    std::printf("    authored-producer dPos=%.4f dRotRad=%.6f dMove=%d dSpeed=%.4f dTurn=%.4f\n",
                result.producerPositionDelta, result.producerRotationDelta,
                authored.authoredMoveTimer - oracle.moveTimer, result.producerSpeedDelta, result.producerTurnDelta);
    std::printf("    cursor-selfcheck oracle=(%.6f,%.8f) soh=(%.6f,%.8f) tol=(%.6f,%.8f)\n",
                result.oracleSelfPositionDelta, result.oracleSelfRotationDelta, result.sohSelfPositionDelta,
                result.sohSelfRotationDelta, HarnessBossFdComparison::kRingSelfPositionTolerance,
                HarnessBossFdComparison::kRingSelfRotationTolerance);
    std::printf("  bossfd summary samples=%zu meanPos=%.4f maxPos=%.4f meanRot=%.6f maxRot=%.6f "
                "tolPos=%.4f tolRot=%.6f tolSpeed=%.4f tolTurn=%.4f verdict=%s reason=%s\n",
                Zelda3D::BossFdHistoryLayout::kBodyOffset.size(), result.positionMean, result.positionMax,
                result.rotationMean, result.rotationMax, HarnessBossFdComparison::kPositionTolerance,
                HarnessBossFdComparison::kRotationTolerance, HarnessBossFdComparison::kSpeedTolerance,
                HarnessBossFdComparison::kTurnRateTolerance, BossFdCompareStatusName(result.status),
                HarnessBossFdComparison::ReasonName(result.reason));
    return RecordStatus(result.status);
}
