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
    char channels[512];
    // Sized to hold the whole list plus the prefix. An undersized buffer here truncated the list to
    // the first two channels, which defeats the only reason the typo path prints one.
    char output[sizeof(channels) + 64];
    if (sscanf(line, "%*s %31s %i", name, &enabled) == 2 && enabled >= 0) {
        if (Zelda3D_LogSet(name, enabled)) {
            Zelda3D_ReplReply(outPath, "log %s=%d", name, enabled ? 1 : 0);
        } else {
            // The channel list, because a typo with nothing to check it against is the one way this
            // command can be useless. The old hint said "try `log list`", which was wrong twice over:
            // there is no `list` subcommand -- a bare `log` is what lists.
            Zelda3D_LogList(channels, static_cast<int>(sizeof(channels)));
            snprintf(output, sizeof(output), "log: no channel '%s'; have: %s", name, channels);
            Zelda3D_ReplReply(outPath, "%s", output);
        }
    } else {
        Zelda3D_LogList(channels, static_cast<int>(sizeof(channels)));
        snprintf(output, sizeof(output), "log channels: %s (env ZELDA3D_LOG=name,.. or all)", channels);
        Zelda3D_ReplReply(outPath, "%s", output);
    }
    return true;
}
