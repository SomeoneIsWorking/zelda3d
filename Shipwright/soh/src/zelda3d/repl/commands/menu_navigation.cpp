#include "menu_navigation.h"

#include "../zelda3d_repl.h"

#include <ship/zelda3d_menu_input.h>

#include <cstdio>
#include <cstring>

namespace {

void RunMenuAction(const char* line, const char* outPath) {
    char argument[64];
    if (std::sscanf(line, "%*s %63s", argument) != 1) {
        Zelda3D_ReplReply(outPath, "menu needs next|prev|activate|close|left|right");
        return;
    }

    int action = -1;
    if (std::strcmp(argument, "next") == 0 || std::strcmp(argument, "down") == 0) {
        action = 0;
    } else if (std::strcmp(argument, "prev") == 0 || std::strcmp(argument, "up") == 0) {
        action = 1;
    } else if (std::strcmp(argument, "activate") == 0 || std::strcmp(argument, "enter") == 0 ||
               std::strcmp(argument, "a") == 0) {
        action = 2;
    } else if (std::strcmp(argument, "close") == 0 || std::strcmp(argument, "esc") == 0 ||
               std::strcmp(argument, "toggle") == 0) {
        action = 3;
    } else if (std::strcmp(argument, "right") == 0 || std::strcmp(argument, "nexttab") == 0) {
        action = 4;
    } else if (std::strcmp(argument, "left") == 0 || std::strcmp(argument, "prevtab") == 0) {
        action = 5;
    }

    if (action < 0) {
        Zelda3D_ReplReply(outPath, "menu: unknown action '%s' (next|prev|activate|close|left|right)", argument);
        return;
    }
    Zelda3D_RmlMenuKey(action);
    Zelda3D_ReplReply(outPath, "menu %s", argument);
}

} // namespace

bool Zelda3D_MenuNavigationReplCommand(const char* command, const char* line, const char* outPath) {
    if (std::strcmp(command, "menu") == 0) {
        RunMenuAction(line, outPath);
    } else if (std::strcmp(command, "menuclick") == 0) {
        float x;
        float y;
        if (std::sscanf(line, "%*s %f %f", &x, &y) != 2) {
            Zelda3D_ReplReply(outPath, "menuclick needs x y");
        } else {
            Zelda3D_RmlMenuClick(static_cast<int>(x), static_cast<int>(y));
            Zelda3D_ReplReply(outPath, "menuclick (%d,%d)", static_cast<int>(x), static_cast<int>(y));
        }
    } else if (std::strcmp(command, "menurow") == 0) {
        const char* argument = std::strchr(line, ' ');
        char report[512] = {};
        Zelda3D_MenuActivateRow(argument != nullptr ? argument + 1 : "", report, static_cast<int>(sizeof(report)));
        Zelda3D_ReplReply(outPath, "%s", report);
    } else {
        return false;
    }
    return true;
}
