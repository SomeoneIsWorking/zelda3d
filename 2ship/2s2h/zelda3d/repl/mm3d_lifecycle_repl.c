#include "2s2h/zelda3d/repl/mm3d_lifecycle_repl.h"

#include <libultraship/bridge/windowbridge.h>
#include <stdio.h>

int Zelda3D_MmLifecycleReplDispatch(PlayState* play, const char* command, Zelda3DMmReplReply reply, void* user) {
    (void)play;
    Zelda3DMmReplArgs args;
    if (Zelda3D_MmReplMatch(command, "switchgame", &args)) {
        char gameId[32];
        if (!Zelda3D_MmReplNextToken(&args, gameId, sizeof(gameId)) || !Zelda3D_MmReplArgsEnd(&args)) {
            reply("switchgame: needs a game id (oot|mm) -- NOTHING DONE", user);
        } else {
            char output[128];
            snprintf(output, sizeof(output), "switchgame %s: ending MM; the launcher takes it from here", gameId);
            reply(output, user);
            WindowRequestGameSwitch(gameId);
        }
        return 1;
    }
    if (Zelda3D_MmReplMatch(command, "quitteardown", &args)) {
        if (!Zelda3D_MmReplArgsEnd(&args)) {
            reply("usage: quitteardown", user);
        } else {
            reply("quitteardown: exit requested WITH full engine teardown (C057 falsifier)", user);
            WindowRequestExitWithFullTeardown();
        }
        return 1;
    }
    if (Zelda3D_MmReplMatch(command, "quit", &args)) {
        if (!Zelda3D_MmReplArgsEnd(&args)) {
            reply("usage: quit", user);
        } else {
            reply("quit: exit requested; taking the orderly window-close shutdown", user);
            WindowRequestExit();
        }
        return 1;
    }
    return 0;
}
