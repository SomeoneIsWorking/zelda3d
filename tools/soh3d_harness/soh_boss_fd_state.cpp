#include "soh_boss_fd_state.h"

#include <cstring>

#include "global.h"
#include "overlays/actors/ovl_Boss_Fd/z_boss_fd.h"
#include "overlays/actors/ovl_Boss_Fd2/z_boss_fd2.h"
#include "zelda3d/behaviors/actor/boss_fd/authored_flight.h"
#include "zelda3d/behaviors/actor/boss_fd.h"
#include "zelda3d/behaviors/actor/boss_fd/forced_flight.h"
#include "zelda3d/behaviors/actor/boss_fd2.h"
#include "zelda3d/behaviors/actor/boss_fd2_bridge.h"

namespace {

Actor* FindBossActor(s16 actorId) {
    if (gPlayState == nullptr) {
        return nullptr;
    }

    ActorListEntry* bosses = &gPlayState->actorCtx.actorLists[ACTORCAT_BOSS];
    int remaining = bosses->length + 4;
    for (Actor* actor = bosses->head; actor != nullptr && remaining-- > 0; actor = actor->next) {
        if (actor->id == actorId) {
            return actor;
        }
    }
    return nullptr;
}

BossFd* FindBossFd() {
    return reinterpret_cast<BossFd*>(FindBossActor(ACTOR_BOSS_FD));
}

} // namespace

extern "C" {

int SohState_BossFdAuthoredState(BossFdAuthoredState* outState, float* outPos3, float* outRot3, int capacity) {
    if (outState == nullptr || outPos3 == nullptr || outRot3 == nullptr || capacity < BOSS_FD_HISTORY_COUNT) {
        return 0;
    }

    BossFd* boss = FindBossFd();
    if (boss == nullptr) {
        return 0;
    }

    return Zelda3D_BossFdAuthoredStateSnapshot(
        &boss->actor, &outState->bodyLead, &outState->sampleCount, &outState->authoredMoveTimer, outState->visualPos,
        outState->visualRot, outState->visualVelocity, &outState->visualSpeed, &outState->visualTurnRate,
        &outState->appliedFlySpeedControl, outPos3, outRot3, capacity);
}

int SohState_BossFdNativeInputs(BossFdNativeInputs* outInputs) {
    if (outInputs == nullptr) {
        return 0;
    }

    BossFd* boss = FindBossFd();
    if (boss == nullptr) {
        return 0;
    }

    outInputs->action = boss->work[BFD_ACTION_STATE];
    outInputs->moveTimer = boss->work[BFD_MOVE_TIMER];
    outInputs->actionTimer = boss->timers[0];
    outInputs->startAttack = boss->work[BFD_START_ATTACK];
    outInputs->stopFlag = boss->work[BFD_STOP_FLAG];
    outInputs->introState = boss->introState;
    outInputs->targetPosition[0] = boss->targetPosition.x;
    outInputs->targetPosition[1] = boss->targetPosition.y;
    outInputs->targetPosition[2] = boss->targetPosition.z;
    outInputs->speed = boss->actor.speedXZ;
    outInputs->flySpeed = boss->fwork[BFD_FLY_SPEED];
    outInputs->turnRate = boss->fwork[BFD_TURN_RATE];
    outInputs->turnRateMax = boss->fwork[BFD_TURN_RATE_MAX];
    outInputs->flyWobbleAmplitude = boss->fwork[BFD_FLY_WOBBLE_AMP];
    outInputs->flyWobbleRate = boss->fwork[BFD_FLY_WOBBLE_RATE];
    outInputs->displacement[0] = boss->actor.colChkInfo.displacement.x;
    outInputs->displacement[1] = boss->actor.colChkInfo.displacement.y;
    outInputs->displacement[2] = boss->actor.colChkInfo.displacement.z;
    return 1;
}

int SohState_BossFdIdentity(uintptr_t* outAddress) {
    BossFd* boss = FindBossFd();
    if (boss == nullptr || outAddress == nullptr) {
        return 0;
    }
    *outAddress = reinterpret_cast<uintptr_t>(boss);
    return 1;
}

int SohState_BossFdForceFlight(uintptr_t* outAddress) {
    BossFd* boss = FindBossFd();
    if (boss == nullptr || outAddress == nullptr || !Zelda3D_BossFdForceFly(&boss->actor)) {
        return 0;
    }
    *outAddress = reinterpret_cast<uintptr_t>(boss);
    return 1;
}

int SohState_BossFdForceFlightSeeded(const float* pos3, const short* rot3, uintptr_t* outAddress) {
    if (pos3 == nullptr || rot3 == nullptr || outAddress == nullptr) {
        return 0;
    }
    BossFd* boss = FindBossFd();
    if (boss == nullptr || !Zelda3D_BossFdForceFlySeeded(&boss->actor, pos3, rot3)) {
        return 0;
    }
    *outAddress = reinterpret_cast<uintptr_t>(boss);
    return 1;
}

int SohState_BossFd2ForceGround(uintptr_t* outAddress) {
    Actor* boss = FindBossActor(ACTOR_BOSS_FD2);
    if (boss == nullptr || outAddress == nullptr || !Zelda3D_BossFd2ForceGround(boss)) {
        return 0;
    }
    *outAddress = reinterpret_cast<uintptr_t>(boss);
    return 1;
}

int SohState_BossFd2RenderedAnchor(float outHead[3], short* outShapeYaw) {
    Actor* boss = FindBossActor(ACTOR_BOSS_FD2);
    if (boss == nullptr || outShapeYaw == nullptr || !Zelda3D_BossFd2RenderedHeadWorldPos(boss, outHead)) {
        return 0;
    }
    *outShapeYaw = boss->shape.rot.y;
    return 1;
}

int SohState_BossFd2Mane(BossFd2ManeState* outState) {
    BossFd2* boss = reinterpret_cast<BossFd2*>(FindBossActor(ACTOR_BOSS_FD2));
    if (boss == nullptr || outState == nullptr) {
        return 0;
    }
    const BossFd2Mane* manes[3] = { &boss->centerMane, &boss->rightMane, &boss->leftMane };
    for (int chain = 0; chain < 3; ++chain) {
        outState->head[chain][0] = manes[chain]->head.x;
        outState->head[chain][1] = manes[chain]->head.y;
        outState->head[chain][2] = manes[chain]->head.z;
        for (int segment = 0; segment < 10; ++segment) {
            outState->pos[chain][segment][0] = manes[chain]->pos[segment].x;
            outState->pos[chain][segment][1] = manes[chain]->pos[segment].y;
            outState->pos[chain][segment][2] = manes[chain]->pos[segment].z;
        }
    }
    return 1;
}

int SohState_BossFd2SyncMane(const float worldPos[3]) {
    BossFd2* boss = reinterpret_cast<BossFd2*>(FindBossActor(ACTOR_BOSS_FD2));
    if (boss == nullptr || worldPos == nullptr) {
        return 0;
    }
    boss->actor.world.pos = { worldPos[0], worldPos[1], worldPos[2] };
    boss->actor.prevPos = boss->actor.world.pos;
    if (!Zelda3D_BossFd2PrepareRenderedMane(&boss->actor)) {
        return 0;
    }
    BossFd2Mane* manes[3] = { &boss->centerMane, &boss->rightMane, &boss->leftMane };
    for (BossFd2Mane* mane : manes) {
        std::memset(mane->rot, 0, sizeof(mane->rot));
        std::memset(mane->pull, 0, sizeof(mane->pull));
        for (Vec3f& pos : mane->pos) {
            pos = mane->head;
        }
    }
    return 1;
}

int SohState_BossFdRenderInfo(BossFdRenderInfo* outInfo) {
    if (outInfo == nullptr) {
        return 0;
    }
    BossFd* boss = FindBossFd();
    Zelda3D::BossFdRenderStatus status;
    if (boss == nullptr || !Zelda3D::bossFdRenderStatus(&boss->actor, &status)) {
        return 0;
    }
    for (int index = 0; index < Zelda3D::BossFdRenderStatus::kModelCount; ++index) {
        outInfo->modelIds[index] = status.modelIds[index];
        outInfo->submitCounts[index] = status.submitCounts[index];
    }
    outInfo->drawAttempts = status.drawAttempts;
    outInfo->drawSuccesses = status.drawSuccesses;
    outInfo->skinSegments = status.skinSegments;
    return 1;
}

} // extern "C"
