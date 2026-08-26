// OoT3D Boss_Fd 30 Hz authored flight producer and procedural-history state.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_AUTHORED_FLIGHT_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_AUTHORED_FLIGHT_H

#include "history_layout.h"

#include "asset/mat4.h"
#include "global.h"

#include <array>
#include <cstdint>

struct BossFd;

namespace Zelda3D::BossFdFlight {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kBinangToRad = kPi / 32768.0f;
inline constexpr float kHeadForwardOffset = 25.0f;
inline constexpr int kHistoryCount = BossFdHistoryLayout::kBodyCount;
inline constexpr int kManeHistoryCount = BossFdHistoryLayout::kManeCount;
inline constexpr const auto& kBodyHistoryOffset = BossFdHistoryLayout::kBodyOffset;

struct State {
    float head = 0.0f;
    float leftArm = 0.0f;
    float rightArm = 0.0f;
    int bodyLead = 0;
    int maneLead = 0;
    s16 authoredMoveTimer = 0;
    s16 lastNativeMoveTimer = 0;
    u8 authoredPhase = 0;
    bool nativeTimerObserved = false;
    uint32_t samplesProduced = 0;
    float flattenMane = 1.0f;
    Vec3f visualPos = {};
    Vec3s visualRot = {};
    Vec3f visualVelocity = {};
    float visualSpeed = 0.0f;
    float visualTurnRate = 0.0f;
    // The fly-speed control the authored ticks last applied (see kFlySpeedControl in the .cpp).
    // Read this — not the shared fwork slot — to know what the authored integration consumes:
    // the N64-path overlay rewrites fwork after our pre-update on every host frame.
    float appliedFlySpeedControl = 0.0f;
    bool bodyRootReady = false;
    bool maneAnchorsReady = false;
    Mat4 bodyRoot = matId();
    std::array<Vec3f, 3> maneAnchors = {};
    std::array<Vec3f, kHistoryCount> bodyPos = {};
    std::array<Vec3f, kHistoryCount> bodyRot = {};
    std::array<std::array<Vec3f, kManeHistoryCount>, 3> manePos = {};
    std::array<Vec3f, kManeHistoryCount> maneRot = {};
    std::array<std::array<float, kManeHistoryCount>, 3> maneScale = {};
};

State& state(Actor* actor);
const State* findState(const Actor* actor);
void reset(Actor* actor);
void preUpdate(BossFd* boss, int bodyModelId);

inline int wrapIndex(int value, int count) {
    value %= count;
    return value < 0 ? value + count : value;
}

} // namespace Zelda3D::BossFdFlight

extern "C" {
int Zelda3D_BossFdHistoryInfo(Actor* actor, int* bodyLead, int* maneLead, Vec3f* minPos, Vec3f* maxPos,
                              int* sampleCount);
int Zelda3D_BossFdAuthoredStateSnapshot(Actor* actor, int* outLead, int* outSampleCount, int* outAuthoredMoveTimer,
                                        float* outVisualPos3, short* outVisualRot3, float* outVisualVelocity3,
                                        float* outVisualSpeed, float* outVisualTurnRate,
                                        float* outAppliedFlySpeedControl, float* outBodyPos3, float* outBodyRot3,
                                        int capacity);
}

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_AUTHORED_FLIGHT_H
