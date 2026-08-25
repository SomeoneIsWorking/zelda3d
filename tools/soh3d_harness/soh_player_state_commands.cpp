#include "soh_player_state_commands.h"

#include <cstdio>

#include "repl_protocol.h"
#include "soh_player_state.h"
#include "soh_runtime.h"

namespace HarnessSohPlayerState {
namespace {

bool RequireBooted(const char* errorMessage) {
    if (HarnessSohRuntime::IsBooted()) {
        return true;
    }
    HarnessRepl::PrintErr(errorMessage);
    return false;
}

bool HandleControlFlags() {
    if (!RequireBooted("soh_ctlflags: run soh_boot first")) {
        return true;
    }
    unsigned int stateFlags1 = 0;
    unsigned int cutsceneIndex = 0;
    unsigned int nextCutsceneIndex = 0;
    int cutsceneState = 0;
    int transitionTrigger = 0;
    int cutsceneAction = 0;
    if (!SohState_DumpControlFlags(&stateFlags1, &cutsceneState, &cutsceneIndex, &nextCutsceneIndex, &transitionTrigger,
                                   &cutsceneAction)) {
        HarnessRepl::PrintErr("soh_ctlflags: no playstate");
        return true;
    }
    std::printf("ok stateFlags1=0x%08x csState=%d cutsceneIndex=0x%04x "
                "nextCsIndex=0x%04x transitionTrigger=%d csAction=%d\n",
                stateFlags1, cutsceneState, cutsceneIndex, nextCutsceneIndex, transitionTrigger, cutsceneAction);
    return true;
}

bool HandleWallInfo() {
    if (!RequireBooted("soh_wallinfo: run soh_boot first")) {
        return true;
    }
    unsigned int backgroundFlags = 0;
    int wallYaw = 0;
    int wallBackgroundId = 0;
    unsigned long wallPolygon = 0;
    float speedXz = 0.0f;
    float velocityY = 0.0f;
    if (!SohState_PlayerWallInfo(&backgroundFlags, &wallYaw, &wallBackgroundId, &wallPolygon, &speedXz, &velocityY)) {
        HarnessRepl::PrintErr("soh_wallinfo: no player");
        return true;
    }
    std::printf("ok bgFlags=0x%04x wallYaw=%d wallBgId=%d wallPoly=0x%lx speedXZ=%.3f velY=%.3f\n", backgroundFlags,
                wallYaw, wallBackgroundId, wallPolygon, speedXz, velocityY);
    return true;
}

} // namespace

bool HandleCommand(const std::string& command, std::istringstream&) {
    if (command == "soh_ctlflags") {
        return HandleControlFlags();
    }
    if (command == "soh_wallinfo") {
        return HandleWallInfo();
    }
    return false;
}

} // namespace HarnessSohPlayerState
