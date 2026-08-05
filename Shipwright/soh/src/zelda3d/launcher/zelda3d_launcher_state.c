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
#include "z64.h"
#include <stdio.h>

// Declared here rather than in a widely-included header: touching functions.h rebuilds the whole
// tree, and these three are only needed by this file.
void Zelda3D_LauncherShow(int show); // SohRmlUi.cpp
void Zelda3D_LaunchMM(void);         // zelda3d_launcher.cpp -- STOPGAP process hand-off, see N3
void Zelda3D_LauncherExit(void);
extern int gZelda3dLauncherAction;   // SohRmlUi.cpp; 1 = OoT, 2 = MM, 3 = quit

void Launcher_Destroy(GameState* gameState) {
    Zelda3D_LauncherShow(0);
}

void Launcher_Main(GameState* gameState) {
    // gZelda3dLauncherAction is set by the launcher document's rows (SohRmlUi). 0 = still choosing.
    static int sTick = 0;
    if (sTick++ == 0) { fprintf(stderr, "SOH3D LAUNCHER: gamestate Main running\n"); fflush(stderr); }
    const int choice = gZelda3dLauncherAction;
    if (choice != 0) { fprintf(stderr, "SOH3D LAUNCHER: choice=%d\n", choice); fflush(stderr); }
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
        // Majora's Mask. ONE BINARY RUNNING BOTH GAMES IS NOT DONE -- it is milestone N3 in
        // docs/MM_NATIVE.md, and it is not a small thing: MM is a separate N64 decomp whose symbols
        // (Play_Init, Actor_Init, gSaveContext, ...) collide with OoT's, which is precisely why the
        // two are separate targets today. Their own design note calls for per-game MODULES (separate
        // binary or shared object) rather than one link unit. Until that exists this hands off by
        // replacing the process, which is application switching and is explicitly NOT what was
        // asked for -- it is a stopgap, and it is labelled as one rather than passed off as done.
        // STOPGAP: run MM in-process via per-game modules (N3) because both cores cannot be linked
        // into one binary without namespacing one of the two decomps.
        Zelda3D_LaunchMM();
        // Only reached if the hand-off failed; stay on the launcher rather than booting the wrong
        // game, and Zelda3D_LaunchMM has already said why on stderr.
        Zelda3D_LauncherShow(1);
    } else if (choice == 3) {
        gameState->running = false;
        Zelda3D_LauncherExit();
    }
}

void Launcher_Init(GameState* gameState) {
    fprintf(stderr, "SOH3D LAUNCHER: gamestate Init\n"); fflush(stderr);
    gameState->main = Launcher_Main;
    gameState->destroy = Launcher_Destroy;
    // Show the document. It is the only thing on screen: no gamestate before this one drew anything.
    Zelda3D_LauncherShow(1);
}
