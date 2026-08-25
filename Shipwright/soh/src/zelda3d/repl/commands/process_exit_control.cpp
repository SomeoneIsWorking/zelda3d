#include "process_exit_control.h"

#include "../zelda3d_repl.h"

#include <ship/Context.h>

#include <cstring>

bool Zelda3D_ProcessExitControlReplCommand(const char* command, const char* outPath) {
    if (std::strcmp(command, "quitteardown") == 0) {
        Zelda3D_ReplReply(outPath, "quitteardown: exit requested WITH full engine teardown (C057 falsifier)");
        Ship::Context::RequestExitWithFullTeardown();
    } else if (std::strcmp(command, "quit") == 0) {
        Zelda3D_ReplReply(outPath, "quit: exit requested; taking the orderly window-close shutdown");
        Ship::Context::RequestExit();
    } else {
        return false;
    }
    return true;
}
