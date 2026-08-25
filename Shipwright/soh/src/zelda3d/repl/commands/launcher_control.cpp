#include "launcher_control.h"

#include "../zelda3d_repl.h"

#include <libultraship/bridge/windowbridge.h>
#include <ship/zelda3d_launcher_bridge.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void RunLauncher(const char* line, const char* outPath) {
    char argument[32] = {};
    char choice[32] = {};
    const int argumentCount = std::sscanf(line, "%*s %31s %31s", argument, choice);
    if (argumentCount <= 0) {
        Zelda3D_ReplReply(outPath, "launcher visible=%d (usage: launcher 0|1 | launcher pick oot|mm|quit)",
                          Zelda3D_LauncherIsVisible());
        return;
    }
    if (std::strcmp(argument, "pick") != 0) {
        Zelda3D_LauncherShow(std::atoi(argument));
        Zelda3D_ReplReply(outPath, "launcher visible=%d", Zelda3D_LauncherIsVisible());
        return;
    }

    const int action = (argumentCount >= 2 && std::strcmp(choice, "oot") == 0)    ? 1
                       : (argumentCount >= 2 && std::strcmp(choice, "mm") == 0)   ? 2
                       : (argumentCount >= 2 && std::strcmp(choice, "quit") == 0) ? 3
                                                                                  : 0;
    if (action == 0) {
        Zelda3D_ReplReply(outPath, "launcher pick: expected oot|mm|quit, got '%s' -- NOTHING DONE",
                          argumentCount >= 2 ? choice : "(nothing)");
        return;
    }
    gZelda3dLauncherAction = action;
    Zelda3D_ReplReply(outPath, "launcher pick %s -> action %d queued", choice, action);
}

} // namespace

bool Zelda3D_LauncherControlReplCommand(const char* command, const char* line, const char* outPath) {
    if (std::strcmp(command, "switchgame") == 0) {
        char gameId[64];
        if (std::sscanf(line, "%*s %63s", gameId) != 1) {
            Zelda3D_ReplReply(outPath, "switchgame needs oot|mm");
        } else {
            Zelda3D_ReplReply(outPath, "switchgame %s: ending this game; the launcher takes it from here", gameId);
            WindowRequestGameSwitch(gameId);
        }
    } else if (std::strcmp(command, "menuhit") == 0) {
        char report[4096] = {};
        Zelda3D_LauncherHitReport(report, static_cast<int>(sizeof(report)));
        Zelda3D_ReplReply(outPath, "%s", report);
    } else if (std::strcmp(command, "launcher") == 0) {
        RunLauncher(line, outPath);
    } else {
        return false;
    }
    return true;
}
