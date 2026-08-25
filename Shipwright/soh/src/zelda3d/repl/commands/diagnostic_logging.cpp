#include "diagnostic_logging.h"

#include "../../core/zelda3d_log.h"
#include "../zelda3d_repl.h"

#include <stdio.h>
#include <string.h>

bool Zelda3D_DiagnosticLoggingReplCommand(const char* command, const char* line, const char* outPath) {
    if (strcmp(command, "log") != 0) {
        return false;
    }

    char name[32] = { 0 };
    int enabled = -1;
    if (sscanf(line, "%*s %31s %i", name, &enabled) == 2 && enabled >= 0) {
        if (Zelda3D_LogSet(name, enabled)) {
            Zelda3D_ReplReply(outPath, "log %s=%d", name, enabled ? 1 : 0);
        } else {
            Zelda3D_ReplReply(outPath, "log: unknown channel '%s' (try `log list`)", name);
        }
    } else {
        char channels[512];
        Zelda3D_LogList(channels, static_cast<int>(sizeof(channels)));
        Zelda3D_ReplReply(outPath, "log channels: %s (env ZELDA3D_LOG=name,.. or all)", channels);
    }
    return true;
}
