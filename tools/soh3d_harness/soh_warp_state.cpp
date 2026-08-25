#include "soh_warp_state.h"

#include "global.h"

extern "C" int SohState_Warp(unsigned short entrance) {
    if (gPlayState == nullptr) {
        return 0;
    }
    gPlayState->csCtx.state = CS_STATE_IDLE;
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    gSaveContext.cutsceneIndex = 0;
    gSaveContext.nextCutsceneIndex = 0xFFEF;
    gPlayState->nextEntranceIndex = static_cast<s16>(entrance);
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_INSTANT;
    gSaveContext.nextTransitionType = TRANS_TYPE_INSTANT;
    gSaveContext.entranceIndex = static_cast<s16>(entrance);
    return 1;
}
