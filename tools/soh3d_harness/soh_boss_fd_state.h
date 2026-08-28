#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_BOSS_FD_STATE_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_BOSS_FD_STATE_H

#include <cstdint>

inline constexpr int BOSS_FD_HISTORY_COUNT = 150;

struct BossFdAuthoredState {
    int bodyLead;
    int sampleCount;
    int authoredMoveTimer;
    float visualPos[3];
    short visualRot[3];
    float visualVelocity[3];
    float visualSpeed;
    float visualTurnRate;
    // Fly-speed control the authored producer last applied. The raw fwork slot is rewritten by
    // the N64-path overlay after every authored pre-update, so it is not the authored value.
    float appliedFlySpeedControl;
};

struct BossFdNativeInputs {
    int action;
    int moveTimer;
    int actionTimer;
    int startAttack;
    int stopFlag;
    int introState;
    float targetPosition[3];
    float speed;
    float flySpeed;
    float turnRate;
    float turnRateMax;
    float flyWobbleAmplitude;
    float flyWobbleRate;
    float displacement[3];
};

struct BossFdRenderInfo {
    int modelIds[8];
    long submitCounts[8];
    int drawAttempts;
    int drawSuccesses;
    int skinSegments;
};

struct BossFd2ManeState {
    float head[3][3];
    float pos[3][10][3];
};

extern "C" {
int SohState_BossFdAuthoredState(BossFdAuthoredState* outState, float* outPos3, float* outRot3, int capacity);
int SohState_BossFdNativeInputs(BossFdNativeInputs* outInputs);
int SohState_BossFdIdentity(std::uintptr_t* outAddress);
int SohState_BossFdForceFlight(std::uintptr_t* outAddress);
int SohState_BossFdForceFlightSeeded(const float* pos3, const short* rot3, std::uintptr_t* outAddress);
int SohState_BossFd2ForceGround(std::uintptr_t* outAddress);
int SohState_BossFd2RenderedAnchor(float outHead[3], short* outShapeYaw);
int SohState_BossFd2Mane(BossFd2ManeState* outState);
int SohState_BossFd2SyncMane(const float worldPos[3], const short worldRot[3], const short shapeRot[3],
                             const short headRot[3], short timer, float jawOpening);
int SohState_BossFd2ResetManeHistories();
int SohState_BossFd2SetManeRootDrivers(const float worldPos[3], const short worldRot[3], const short shapeRot[3],
                                       const short headRot[3], short timer, float jawOpening);
int SohState_BossFd2AnimationFrame(float* outFrame);
int SohState_BossFd2CaptureManeAnimController();
int SohState_BossFd2RestoreManeAnimController();
int SohState_BossFdRenderInfo(BossFdRenderInfo* outInfo);
}

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_BOSS_FD_STATE_H
