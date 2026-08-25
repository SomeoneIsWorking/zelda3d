#include "cvar_control.h"

#include "../zelda3d_repl.h"
#include <libultraship/bridge/consolevariablebridge.h>

#include <stdio.h>
#include <string.h>

bool Zelda3D_CVarControlReplCommand(const char* command, const char* line, const char* outPath) {
    if (strcmp(command, "cvari") != 0) {
        return false;
    }

    char name[128];
    int value;
    if (sscanf(line, "%*s %127s %i", name, &value) != 2) {
        return false;
    }
    CVarSetInteger(name, value);
    CVarSave();
    Zelda3D_ReplReply(outPath, "cvari %s = %d (read back %d)", name, value, CVarGetInteger(name, -999));
    return true;
}
