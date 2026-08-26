// OoT3D Boss_Fd 30 Hz authored flight producer and procedural-history state.
#include "authored_flight.h"
#include "steering_math.h"
#include "functions/math.h"

#include "../../../anim/pose_evaluation.h"
#include "object/ObjectExtension.h"
#include "overlays/actors/ovl_Boss_Fd/z_boss_fd.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Zelda3D::BossFdFlight {
namespace {

constexpr float kAuthoredFramesPerUpdate = 2.0f / 3.0f;

// FUN_003C724C (the flying action) rewrites the fly-speed control from its literal pool every
// authored tick while its substate (+0x229E, our `introState`) is zero: `*(+0x90C) = *(0x003c76d0)`.
// The pool word reads 0x40555555 = 10/3. Without mirroring this writeback the N64-path overlay's
// own `fwork[BFD_FLY_SPEED] = 5.0f` (z_boss_fd.c) leaks into the authored integration, and no
// forced profile can hold. Keep in exact agreement with BossFdForcedProfile::kSpeed.
inline constexpr float kFlySpeedControl = 10.0f / 3.0f;

static ObjectExtension::Register<State> StateRegister;

void updateManeAnchors(BossFd* boss, State& flight, float shapeRotZ, int bodyModelId) {
    const int headHistory = wrapIndex(flight.bodyLead + kBodyHistoryOffset[0], kHistoryCount);
    const Vec3f& headPos = flight.bodyPos[headHistory];
    const Vec3f& headRot = flight.bodyRot[headHistory];
    Mat4 head = matMul(matT(headPos.x, headPos.y, headPos.z), matMul(matRy(headRot.y), matRx(-headRot.x)));
    head = matMul(head, matRz(shapeRotZ));
    head = matMul(head, matT(0.0f, 0.0f, kHeadForwardOffset));
    if (!flight.bodyRootReady) {
        flight.bodyRootReady = Zelda3D_AnimWorldBone(bodyModelId, "vb_FWDtest", 0.0f, 0, flight.bodyRoot.data()) != 0;
    }
    if (!flight.bodyRootReady)
        return;

    head = matMul(head, flight.bodyRoot);
    head = matMul(head, matS(boss->actor.scale.x * 0.1f, boss->actor.scale.y * 0.1f, boss->actor.scale.z * 0.1f));
    constexpr float kAnchors[3][3] = { { 0.0f, 2500.0f, 3000.0f },
                                       { -1000.0f, 2500.0f, 3000.0f },
                                       { 1000.0f, 2500.0f, 3000.0f } };
    for (int chain = 0; chain < 3; ++chain) {
        float out[3];
        matApplyPos(head, kAnchors[chain], out);
        flight.maneAnchors[chain] = { out[0], out[1], out[2] };
    }
    flight.maneAnchorsReady = true;
}

void appendHistorySample(BossFd* boss, State& flight, const Vec3f& worldPos, const Vec3f& worldRot, float shapeRotZ,
                         int bodyModelId) {
    if (!flight.maneAnchorsReady) {
        updateManeAnchors(boss, flight, shapeRotZ, bodyModelId);
    }

    flight.bodyLead = wrapIndex(flight.bodyLead + 1, kHistoryCount);
    flight.bodyPos[flight.bodyLead] = worldPos;
    flight.bodyRot[flight.bodyLead] = worldRot;

    // FUN_003C724C stores the anchors left by the preceding FUN_003B4308 draw. Compute the next
    // draw-equivalent anchors only after this slot has consumed that cached state.
    flight.maneLead = wrapIndex(flight.maneLead + 1, kManeHistoryCount);
    flight.maneRot[flight.maneLead] = flight.bodyRot[flight.bodyLead];
    for (int chain = 0; chain < 3; ++chain) {
        flight.manePos[chain][flight.maneLead] = flight.maneAnchors[chain];
    }
    const float move = static_cast<float>(flight.authoredMoveTimer);
    flight.maneScale[0][flight.maneLead] = std::sin(move * 5596.0f * kBinangToRad) * 0.3f + 1.0f;
    flight.maneScale[1][flight.maneLead] = std::sin(move * 5496.0f * kBinangToRad) * 0.3f + 1.0f;
    flight.maneScale[2][flight.maneLead] = std::cos(move * 5696.0f * kBinangToRad) * 0.3f + 1.0f;

    const float targetFlatten =
        (worldRot.x < 0x3000 * kBinangToRad && worldRot.x > -0x3000 * kBinangToRad) ? 1.0f : 0.5f;
    flight.flattenMane += std::clamp(targetFlatten - flight.flattenMane, -0.05f, 0.05f);
    updateManeAnchors(boss, flight, shapeRotZ, bodyModelId);
    ++flight.samplesProduced;
}

void advanceControllers(State& flight) {
    // FUN_003C724C updates the live head, right-arm, and left-arm controllers before its stop gate.
    // Body vb_FWDtest and death-head remain at the frame zero selected by FUN_001A62C4.
    flight.head = std::fmod(flight.head + kAuthoredFramesPerUpdate, 3.0f);
    flight.rightArm = std::fmod(flight.rightArm + kAuthoredFramesPerUpdate, 31.0f);
    flight.leftArm = std::fmod(flight.leftArm + kAuthoredFramesPerUpdate, 3.0f);
}

void advanceFlight(BossFd* boss, State& flight, int bodyModelId) {
    const float move = static_cast<float>(flight.authoredMoveTimer);
    const float wobbleRate = boss->fwork[BFD_FLY_WOBBLE_RATE];
    const float wobbleAmp = boss->fwork[BFD_FLY_WOBBLE_AMP];
    const float dx =
        boss->targetPosition.x - flight.visualPos.x +
        BossFdSteeringMath::SinS(BossFdSteeringMath::WrapBinAngle(move * (wobbleRate + 2096.0F))) * wobbleAmp;
    float dy = boss->targetPosition.y - flight.visualPos.y +
               BossFdSteeringMath::SinS(BossFdSteeringMath::WrapBinAngle(move * (wobbleRate + 1096.0F))) * wobbleAmp;
    const float dz =
        boss->targetPosition.z - flight.visualPos.z +
        BossFdSteeringMath::SinS(BossFdSteeringMath::WrapBinAngle(move * (wobbleRate + 1796.0F))) * wobbleAmp;
    const s16 yawTarget = BossFdSteeringMath::WrapBinAngle(BossFdSteeringMath::Atan2(dx, dz) * (32768.0f / kPi));
    s16 pitchTarget = BossFdSteeringMath::WrapBinAngle(BossFdSteeringMath::Atan2(dy, std::sqrt(dx * dx + dz * dz)) *
                                                       (32768.0f / kPi));

    const int action = boss->work[BFD_ACTION_STATE];
    const s16 turnStep = static_cast<s16>(flight.visualTurnRate * (2.0f / 3.0f));
    Math_ApproachS(&flight.visualRot.y, yawTarget, 10, turnStep);
    if ((action == BOSSFD_FLY_CHASE || action == BOSSFD_FLY_UNUSED) && flight.visualPos.y < 110.0f && pitchTarget < 0) {
        pitchTarget = 0;
        Math_ApproachF(&flight.visualPos.y, 110.0f, 1.0f, 5.0f);
    }
    Math_ApproachS(&flight.visualRot.x, pitchTarget, 10, turnStep);
    Math_ApproachF(&flight.visualTurnRate, boss->fwork[BFD_TURN_RATE_MAX], 1.0f, 20000.0f);
    Math_ApproachF(&flight.visualSpeed, boss->fwork[BFD_FLY_SPEED], 1.0f, 0.1f);
    if (action < BOSSFD_SKULL_FALL) {
        const float cosPitch = BossFdSteeringMath::CosS(flight.visualRot.x);
        flight.visualVelocity.x = BossFdSteeringMath::SinS(flight.visualRot.y) * cosPitch * flight.visualSpeed;
        flight.visualVelocity.y = BossFdSteeringMath::SinS(flight.visualRot.x) * flight.visualSpeed;
        flight.visualVelocity.z = BossFdSteeringMath::CosS(flight.visualRot.y) * cosPitch * flight.visualSpeed;
    } else {
        // FUN_003C724C skips velocity derivation from action 0xCC onward, but still integrates the
        // action-authored velocity through FUN_0036B96C.
        flight.visualVelocity = boss->actor.velocity;
    }
    // FUN_0036B96C's authored-tick scalar is 2 * 0.5 = 1.0. The observed 0.5 displacement per
    // emulator frame came from the actor running at 30 Hz inside a 60 Hz oracle, not from a half-rate
    // integration step. Collision displacement is already integrated and remains unscaled.
    flight.visualPos.x += boss->actor.colChkInfo.displacement.x + flight.visualVelocity.x;
    flight.visualPos.y += boss->actor.colChkInfo.displacement.y + flight.visualVelocity.y;
    flight.visualPos.z += boss->actor.colChkInfo.displacement.z + flight.visualVelocity.z;

    const Vec3f rot = { flight.visualRot.x * kBinangToRad, flight.visualRot.y * kBinangToRad,
                        flight.visualRot.z * kBinangToRad };
    appendHistorySample(boss, flight, flight.visualPos, rot, boss->actor.shape.rot.z * kBinangToRad, bodyModelId);
}

constexpr s16 incrementS16(s16 value) {
    return value == std::numeric_limits<s16>::max() ? std::numeric_limits<s16>::min() : static_cast<s16>(value + 1);
}

} // namespace

State& state(Actor* actor) {
    ObjectExtension& extensions = ObjectExtension::GetInstance();
    State* flight = extensions.Get<State>(actor);
    if (flight == nullptr) {
        extensions.Set<State>(actor, State{});
        flight = extensions.Get<State>(actor);
        const Vec3f rot = { actor->world.rot.x * kBinangToRad, actor->world.rot.y * kBinangToRad,
                            actor->world.rot.z * kBinangToRad };
        flight->bodyPos.fill(actor->world.pos);
        flight->bodyRot.fill(rot);
        flight->maneRot.fill(rot);
        for (auto& chain : flight->manePos)
            chain.fill(actor->world.pos);
        for (auto& chain : flight->maneScale)
            chain.fill(1.0f);
        flight->maneAnchors.fill(actor->world.pos);
        const BossFd* boss = reinterpret_cast<const BossFd*>(actor);
        flight->authoredMoveTimer = boss->work[BFD_MOVE_TIMER];
        flight->lastNativeMoveTimer = boss->work[BFD_MOVE_TIMER];
        flight->visualPos = actor->world.pos;
        flight->visualRot = actor->world.rot;
        flight->visualVelocity = actor->velocity;
        flight->visualSpeed = actor->speedXZ;
        flight->visualTurnRate = boss->fwork[BFD_TURN_RATE];
    }
    return *flight;
}

const State* findState(const Actor* actor) {
    const State* flight = ObjectExtension::GetInstance().Get<State>(actor);
    return flight != nullptr && flight->samplesProduced > 0 ? flight : nullptr;
}

void reset(Actor* actor) {
    ObjectExtension::GetInstance().Remove<State>(actor);
    (void)state(actor);
}

void preUpdate(BossFd* boss, int bodyModelId) {
    State& flight = state(&boss->actor);
    const s16 nativeMoveTimer = boss->work[BFD_MOVE_TIMER];
    if (flight.nativeTimerObserved && nativeMoveTimer != incrementS16(flight.lastNativeMoveTimer)) {
        flight.authoredMoveTimer = nativeMoveTimer;
    }
    flight.nativeTimerObserved = true;
    flight.lastNativeMoveTimer = nativeMoveTimer;

    // The pre-update boundary supplies causal state. A 3:2 integer accumulator produces OoT3D's
    // 30 Hz cadence without drift. Only the constant fdfly profile has paired producer proof;
    // general action and death writes remain partial until their 3DS action producers are ported.
    flight.authoredPhase = static_cast<u8>(flight.authoredPhase + 3);
    const int authoredTicks = flight.authoredPhase / 2;
    flight.authoredPhase %= 2;
    for (int tick = 0; tick < authoredTicks; ++tick) {
        flight.authoredMoveTimer = incrementS16(flight.authoredMoveTimer);
        advanceControllers(flight);
        if (!boss->work[BFD_STOP_FLAG]) {
            // FUN_003C724C writes the fly-speed control before its movement consumes it, each tick.
            if (boss->work[BFD_ACTION_STATE] == BOSSFD_FLY_MAIN && boss->introState == BFD_CS_NONE) {
                boss->fwork[BFD_FLY_SPEED] = kFlySpeedControl;
                flight.appliedFlySpeedControl = kFlySpeedControl;
            }
            advanceFlight(boss, flight, bodyModelId);
        }
    }
}

} // namespace Zelda3D::BossFdFlight

extern "C" int Zelda3D_BossFdAuthoredStateSnapshot(Actor* actor, int* outLead, int* outSampleCount,
                                                   int* outAuthoredMoveTimer, float* outVisualPos3,
                                                   short* outVisualRot3, float* outVisualVelocity3,
                                                   float* outVisualSpeed, float* outVisualTurnRate,
                                                   float* outAppliedFlySpeedControl, float* outBodyPos3,
                                                   float* outBodyRot3, int capacity) {
    using namespace Zelda3D::BossFdFlight;
    if (!actor || actor->id != ACTOR_BOSS_FD || capacity < kHistoryCount || !outLead || !outSampleCount ||
        !outAuthoredMoveTimer || !outVisualPos3 || !outVisualRot3 || !outVisualVelocity3 || !outVisualSpeed ||
        !outVisualTurnRate || !outAppliedFlySpeedControl || !outBodyPos3 || !outBodyRot3) {
        return 0;
    }
    const State* flight = findState(actor);
    if (!flight)
        return 0;

    *outLead = flight->bodyLead;
    *outSampleCount = static_cast<int>(std::min<uint32_t>(flight->samplesProduced, kHistoryCount));
    *outAuthoredMoveTimer = flight->authoredMoveTimer;
    outVisualPos3[0] = flight->visualPos.x;
    outVisualPos3[1] = flight->visualPos.y;
    outVisualPos3[2] = flight->visualPos.z;
    outVisualRot3[0] = flight->visualRot.x;
    outVisualRot3[1] = flight->visualRot.y;
    outVisualRot3[2] = flight->visualRot.z;
    outVisualVelocity3[0] = flight->visualVelocity.x;
    outVisualVelocity3[1] = flight->visualVelocity.y;
    outVisualVelocity3[2] = flight->visualVelocity.z;
    *outVisualSpeed = flight->visualSpeed;
    *outVisualTurnRate = flight->visualTurnRate;
    *outAppliedFlySpeedControl = flight->appliedFlySpeedControl;
    for (int i = 0; i < kHistoryCount; ++i) {
        outBodyPos3[i * 3 + 0] = flight->bodyPos[i].x;
        outBodyPos3[i * 3 + 1] = flight->bodyPos[i].y;
        outBodyPos3[i * 3 + 2] = flight->bodyPos[i].z;
        outBodyRot3[i * 3 + 0] = flight->bodyRot[i].x;
        outBodyRot3[i * 3 + 1] = flight->bodyRot[i].y;
        outBodyRot3[i * 3 + 2] = flight->bodyRot[i].z;
    }
    return kHistoryCount;
}

extern "C" int Zelda3D_BossFdHistoryInfo(Actor* actor, int* bodyLead, int* maneLead, Vec3f* minPos, Vec3f* maxPos,
                                         int* sampleCount) {
    using namespace Zelda3D::BossFdFlight;
    if (!actor || actor->id != ACTOR_BOSS_FD || !bodyLead || !maneLead || !minPos || !maxPos || !sampleCount) {
        return 0;
    }
    const State* flight = findState(actor);
    if (!flight)
        return 0;
    *bodyLead = flight->bodyLead;
    *maneLead = flight->maneLead;
    *sampleCount = static_cast<int>(std::min<uint32_t>(flight->samplesProduced, flight->bodyPos.size()));
    *minPos = flight->bodyPos[0];
    *maxPos = flight->bodyPos[0];
    for (size_t i = 1; i < flight->bodyPos.size(); ++i) {
        minPos->x = std::min(minPos->x, flight->bodyPos[i].x);
        minPos->y = std::min(minPos->y, flight->bodyPos[i].y);
        minPos->z = std::min(minPos->z, flight->bodyPos[i].z);
        maxPos->x = std::max(maxPos->x, flight->bodyPos[i].x);
        maxPos->y = std::max(maxPos->y, flight->bodyPos[i].y);
        maxPos->z = std::max(maxPos->z, flight->bodyPos[i].z);
    }
    return 1;
}
