#include "boss_fd_compare.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "boss_fd_comparison_policy.h"
#include "boss_fd_control.h"
#include "boss_fd_oracle.h"
#include "core/core.h"
#include "core/memory.h"
#include "soh_boss_fd_state.h"
#include "soh_play_state.h"

namespace {

BossFdCompareStatus gLastCompareStatus = BossFdCompareStatus::Invalid;
BossFdCompareStatus gLastManeCompareStatus = BossFdCompareStatus::Invalid;

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
    std::printf("    producer-state oracle pos=(%.4f,%.4f,%.4f) rot=(%d,%d,%d) vel=(%.6f,%.6f,%.6f) "
                "disp=(%.4f,%.4f,%.4f)\n",
                oracle.worldPos[0], oracle.worldPos[1], oracle.worldPos[2], oracle.worldRot[0], oracle.worldRot[1],
                oracle.worldRot[2], oracle.velocity[0], oracle.velocity[1], oracle.velocity[2], oracle.displacement[0],
                oracle.displacement[1], oracle.displacement[2]);
    std::printf("    producer-state soh pos=(%.4f,%.4f,%.4f) rot=(%d,%d,%d) vel=(%.6f,%.6f,%.6f) "
                "disp=(%.4f,%.4f,%.4f)\n",
                authored.visualPos[0], authored.visualPos[1], authored.visualPos[2], authored.visualRot[0],
                authored.visualRot[1], authored.visualRot[2], authored.visualVelocity[0], authored.visualVelocity[1],
                authored.visualVelocity[2], native.displacement[0], native.displacement[1], native.displacement[2]);
}

void PrintRenderInfo() {
    BossFdRenderInfo info{};
    if (!SohState_BossFdRenderInfo(&info)) {
        std::printf("    render soh=missing\n");
        return;
    }
    std::printf("    render attempts=%d successes=%d skin=%d models=", info.drawAttempts, info.drawSuccesses,
                info.skinSegments);
    for (int index = 0; index < 8; ++index) {
        std::printf("%s%d:%ld", index == 0 ? "" : ",", info.modelIds[index], info.submitCounts[index]);
    }
    std::printf("\n");
}

BossFdCompareStatus RecordStatus(BossFdCompareStatus status) {
    gLastCompareStatus = status;
    return status;
}

} // namespace

BossFdCompareStatus LastBossFdCompareStatus() {
    return gLastCompareStatus;
}

BossFdCompareStatus LastBossFd2ManeCompareStatus() {
    return gLastManeCompareStatus;
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
    PrintRenderInfo();
    std::printf("    authored-producer dPos=%.4f dRotRad=%.6f dVel=%.6f dMove=%d dSpeed=%.4f dTurn=%.4f\n",
                result.producerPositionDelta, result.producerRotationDelta, result.producerVelocityDelta,
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

BossFdCompareStatus CompareBossFd2Mane(uint32_t azPlayState) {
    auto& memory = Core::System::GetInstance().Memory();
    HarnessBossFdOracle::ManeState oracle{};
    BossFd2ManeState soh{};
    if (!HarnessBossFdOracle::ReadHoleMane(memory, azPlayState, &oracle) || !SohState_BossFd2Mane(&soh)) {
        std::printf("  bossfd2_mane state=MISSING\n");
        return gLastManeCompareStatus = BossFdCompareStatus::Missing;
    }

    HarnessBossFd2ManeRootControl::Roots oracleRoots = oracle.head;
    HarnessBossFd2ManeRootControl::Roots sohRoots{};
    for (int chain = 0; chain < 3; ++chain) {
        for (int axis = 0; axis < 3; ++axis) {
            sohRoots[chain][axis] = soh.head[chain][axis];
        }
    }
    const auto rootControl = HarnessBossFdControl::ManeRootControlSnapshot();
    if (rootControl.status != HarnessBossFd2ManeRootControl::Status::Tracking ||
        !HarnessBossFdControl::ManeRootControlAcceptsCurrent(oracleRoots, sohRoots)) {
        std::printf("  bossfd2_mane state=INVALID reason=root-trajectory-uncontrolled observedSteps=%d "
                    "maxStepDelta=%.9g\n",
                    rootControl.observedSteps, rootControl.maximumStepDelta);
        return gLastManeCompareStatus = BossFdCompareStatus::Invalid;
    }

    float total = 0.0f;
    float maximum = 0.0f;
    for (int chain = 0; chain < 3; ++chain) {
        float chainTotal = 0.0f;
        float chainMaximum = 0.0f;
        for (int segment = 0; segment < 10; ++segment) {
            float squared = 0.0f;
            for (int axis = 0; axis < 3; ++axis) {
                const float oracleRelative = oracle.pos[chain][segment][axis] - oracle.head[chain][axis];
                const float sohRelative = soh.pos[chain][segment][axis] - soh.head[chain][axis];
                const float delta = sohRelative - oracleRelative;
                squared += delta * delta;
            }
            const float distance = std::sqrt(squared);
            chainTotal += distance;
            chainMaximum = std::max(chainMaximum, distance);
        }
        total += chainTotal;
        maximum = std::max(maximum, chainMaximum);
        std::printf("  chain=%d mean=%.4f max=%.4f oracleHead=(%.3f,%.3f,%.3f) sohHead=(%.3f,%.3f,%.3f) "
                    "oracleTail=(%.3f,%.3f,%.3f) sohTail=(%.3f,%.3f,%.3f)\n",
                    chain, chainTotal / 10.0f, chainMaximum, oracle.head[chain][0], oracle.head[chain][1],
                    oracle.head[chain][2], soh.head[chain][0], soh.head[chain][1], soh.head[chain][2],
                    oracle.pos[chain][9][0] - oracle.head[chain][0], oracle.pos[chain][9][1] - oracle.head[chain][1],
                    oracle.pos[chain][9][2] - oracle.head[chain][2], soh.pos[chain][9][0] - soh.head[chain][0],
                    soh.pos[chain][9][1] - soh.head[chain][1], soh.pos[chain][9][2] - soh.head[chain][2]);
    }
    std::printf("  bossfd2_mane summary samples=30 mean=%.4f max=%.4f\n", total / 30.0f, maximum);
    std::printf("  bossfd2_mane root-control=MATCH observedSteps=%d maxStepDelta=%.9g verdict=%s\n",
                rootControl.observedSteps, rootControl.maximumStepDelta, maximum == 0.0F ? "MATCH" : "DIVERGED");
    return gLastManeCompareStatus = maximum == 0.0F ? BossFdCompareStatus::Match : BossFdCompareStatus::Diverged;
}
