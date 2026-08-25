#include "soh_play_state.h"

#include "global.h"

extern "C" {

int SohState_HasPlayState(void) {
    return gPlayState != nullptr ? 1 : 0;
}

int SohState_SceneNum(void) {
    return gPlayState != nullptr ? static_cast<int>(gPlayState->sceneNum) : -1;
}

int SohState_RoomNum(void) {
    return gPlayState != nullptr ? static_cast<int>(gPlayState->roomCtx.curRoom.num) : -1;
}

int SohState_CsFrames(void) {
    return gPlayState != nullptr ? static_cast<int>(gPlayState->csCtx.frames) : -1;
}

int SohState_SetCsFrames(int frames) {
    if (gPlayState == nullptr) {
        return 0;
    }
    gPlayState->csCtx.frames = static_cast<uint16_t>(frames & 0xFFFF);
    return 1;
}

} // extern "C"
