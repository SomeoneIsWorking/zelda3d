#include "player_pause_control.h"

#include <cstdio>
#include <cstring>

#include "../../input/pause_navigation.h"
#include "../zelda3d_repl.h"

namespace {

void HandlePause(PlayState* play, const char* line, const char* outPath) {
    char argument[32] = {};
    if (std::sscanf(line, "%*s %31s", argument) == 1) {
        int target = -3;
        if (std::strcmp(argument, "item") == 0) {
            target = PAUSE_ITEM;
        } else if (std::strcmp(argument, "map") == 0) {
            target = PAUSE_MAP;
        } else if (std::strcmp(argument, "quest") == 0) {
            target = PAUSE_QUEST;
        } else if (std::strcmp(argument, "equip") == 0) {
            target = PAUSE_EQUIP;
        } else if (std::strcmp(argument, "close") == 0) {
            target = -2;
        }
        if (target == -3) {
            Zelda3D_ReplReply(outPath, "usage: pause <item|map|quest|equip|close>");
        } else {
            Zelda3D_PauseNavigationSetTarget(target);
            Zelda3D_ReplReply(outPath, "pause -> %s (target=%d)", argument, target);
        }
    } else {
        PauseContext* pauseContext = &play->pauseCtx;
        Zelda3D_ReplReply(outPath, "pause state=%d pageIndex=%d unk_1E4=%d mode=%d target=%d", pauseContext->state,
                          pauseContext->pageIndex, pauseContext->unk_1E4, pauseContext->mode,
                          Zelda3D_PauseNavigationTarget());
    }
}

} // namespace

bool Zelda3D_PlayerPauseControlReplCommand(PlayState* play, const char* command, const char* line,
                                           const char* outPath) {
    if (std::strcmp(command, "pause") != 0) {
        return false;
    }
    HandlePause(play, line, outPath);
    return true;
}
