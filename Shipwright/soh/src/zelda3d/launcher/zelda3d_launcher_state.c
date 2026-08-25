// The launcher as a GAMESTATE — its own screen, with no game running behind it.
//
// The first attempt showed the launcher from inside the running game, so Ocarina of Time had already
// booted and was updating underneath it. Pausing the game would have been a bandaid: the launcher
// should exist BEFORE any game does. The engine already has the right concept for that — a
// gamestate — so the launcher becomes one, ahead of TitleSetup (which is what previously ran first).
//
// Nothing here draws the UI: RmlUi is composited by the Gui pass every frame regardless of
// gamestate, so this state only has to exist, keep the launcher shown, and hand over once a choice
// is made. That is why its Main is nearly empty — the emptiness IS the point. No Play, no Title, no
// actors, no save context.

#include "global.h"
#include "functions/game_state.h"
#include "zelda3d/launcher/zelda3d_launcher.h"
#include "z64.h"
#include <ship/zelda3d_launcher_bridge.h>
#include <stdio.h>

void Launcher_Destroy(GameState* gameState) {
    Zelda3D_LauncherShow(0);
}

void Launcher_Main(GameState* gameState) {
    // gZelda3dLauncherAction is set by the launcher document's rows (SohRmlUi). 0 = still choosing.
    static int sTick = 0;
    if (sTick++ == 0) {
        fprintf(stderr, "SOH3D LAUNCHER: gamestate Main running\n");
        fflush(stderr);
    }
    const int choice = gZelda3dLauncherAction;
    if (choice != 0) {
        fprintf(stderr, "SOH3D LAUNCHER: choice=%d\n", choice);
        fflush(stderr);
    }
    if (choice == 0) {
        return; // stay on this screen
    }
    gZelda3dLauncherAction = 0;

    if (choice == 1) {
        // Ocarina of Time: hand over to the normal boot chain exactly where it used to begin.
        Zelda3D_LauncherShow(0);
        gameState->running = false;
        SET_NEXT_GAMESTATE(gameState, TitleSetup_Init, GameState);
    } else if (choice == 2) {
        // Majora's Mask: a handover inside this one process. Zelda3D_LaunchMM records the request
        // and ends this game; ending it is THIS function's job to finish -- clearing `running` with
        // no next gamestate is what drains RunFrame's overlay loop and gets the core's run() to
        // return to the launcher, which then loads the MM core. A 0 means MM did not start, so stay
        // on the launcher rather than booting the wrong game (it has already said why on stderr).
        if (!Zelda3D_LaunchMM()) {
            Zelda3D_LauncherShow(1);
        } else {
            Zelda3D_LauncherShow(0);
            gameState->running = false; // no SET_NEXT_GAMESTATE: this core is finished
        }
    } else if (choice == 3) {
        gameState->running = false;
        Zelda3D_LauncherExit();
    }
}

void Launcher_Init(GameState* gameState) {
    fprintf(stderr, "SOH3D LAUNCHER: gamestate Init\n");
    fflush(stderr);
    gameState->main = Launcher_Main;
    gameState->destroy = Launcher_Destroy;
    // Show the document. It is the only thing on screen: no gamestate before this one drew anything.
    Zelda3D_LauncherShow(1);
}
