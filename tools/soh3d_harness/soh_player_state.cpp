#include "soh_player_state.h"

#include "global.h"
#include "z64actor.h"
#include "z64player.h"

namespace {

Player* CurrentPlayer() {
    if (gPlayState == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<Player*>(gPlayState->actorCtx.actorLists[ACTORCAT_PLAYER].head);
}

} // namespace

extern "C" {

int SohState_PlayerPos(float* px, float* py, float* pz, short* rx, short* ry, short* rz) {
    const Player* player = CurrentPlayer();
    if (player == nullptr) {
        return 0;
    }
    *px = player->actor.world.pos.x;
    *py = player->actor.world.pos.y;
    *pz = player->actor.world.pos.z;
    *rx = player->actor.world.rot.x;
    *ry = player->actor.world.rot.y;
    *rz = player->actor.world.rot.z;
    return 1;
}

int SohState_PlayerWallInfo(unsigned int* outBgFlags, int* outWallYaw, int* outWallBgId, unsigned long* outWallPoly,
                            float* outSpeedXZ, float* outVelY) {
    const Player* player = CurrentPlayer();
    if (player == nullptr) {
        return 0;
    }
    const Actor& actor = player->actor;
    if (outBgFlags != nullptr) {
        *outBgFlags = static_cast<unsigned int>(actor.bgCheckFlags);
    }
    if (outWallYaw != nullptr) {
        *outWallYaw = static_cast<int>(actor.wallYaw);
    }
    if (outWallBgId != nullptr) {
        *outWallBgId = static_cast<int>(actor.wallBgId);
    }
    if (outWallPoly != nullptr) {
        *outWallPoly = reinterpret_cast<unsigned long>(actor.wallPoly);
    }
    if (outSpeedXZ != nullptr) {
        *outSpeedXZ = actor.speedXZ;
    }
    if (outVelY != nullptr) {
        *outVelY = actor.velocity.y;
    }
    return 1;
}

int SohState_TeleportPlayer(float x, float y, float z) {
    Player* player = CurrentPlayer();
    if (player == nullptr) {
        return 0;
    }
    player->actor.world.pos = { x, y, z };
    return 1;
}

int SohState_SetPlayerYaw(int yawS16) {
    Player* player = CurrentPlayer();
    if (player == nullptr) {
        return 0;
    }
    const short yaw = static_cast<short>(yawS16 & 0xFFFF);
    player->actor.world.rot.y = yaw;
    player->actor.shape.rot.y = yaw;
    player->yaw = yaw;
    return 1;
}

int SohState_SetLinkAge(int age) {
    gSaveContext.linkAge = age != 0 ? 1 : 0;
    if (gPlayState != nullptr) {
        gPlayState->linkAgeOnLoad = static_cast<u8>(gSaveContext.linkAge);
    }
    return 1;
}

int SohState_GetLinkAge(void) {
    return static_cast<int>(gSaveContext.linkAge);
}

int SohState_DumpControlFlags(unsigned int* outStateFlags1, int* outCsState, unsigned int* outCsIndex,
                              unsigned int* outNextCsIndex, int* outTransTrigger, int* outCsAction) {
    if (gPlayState == nullptr) {
        return 0;
    }
    const Player* player = CurrentPlayer();
    if (outStateFlags1 != nullptr) {
        *outStateFlags1 = player != nullptr ? player->stateFlags1 : 0;
    }
    if (outCsState != nullptr) {
        *outCsState = static_cast<int>(gPlayState->csCtx.state);
    }
    if (outCsIndex != nullptr) {
        *outCsIndex = static_cast<unsigned int>(gSaveContext.cutsceneIndex & 0xFFFFU);
    }
    if (outNextCsIndex != nullptr) {
        *outNextCsIndex = static_cast<unsigned int>(gSaveContext.nextCutsceneIndex & 0xFFFFU);
    }
    if (outTransTrigger != nullptr) {
        *outTransTrigger = gPlayState->transitionTrigger;
    }
    if (outCsAction != nullptr) {
        *outCsAction = player != nullptr ? player->csAction : -1;
    }
    return 1;
}

} // extern "C"
