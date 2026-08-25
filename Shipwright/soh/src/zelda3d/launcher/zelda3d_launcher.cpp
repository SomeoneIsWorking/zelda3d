// OoT/MM launcher — the game side of the chooser.
//
// The RmlUi layer (libultraship SohRmlUi) owns the launcher DOCUMENT and records which game was
// chosen in gZelda3dLauncherAction. It deliberately knows nothing about processes. This module is
// the other half: turning "Majora's Mask" into a running MM.
//
// It does that WITHOUT starting a process, because there is only one program. `zelda3d` dlopen'd
// this core and is waiting for its run() to return, so switching games is a handover: record which
// game is wanted, end this one, and the launcher loads the MM core into the same process. The user
// sees the game change; the process, window and renderer never do.
//
// Why the handover tears nothing down: the engine -- window, renderer, resource manager -- is one
// copy in libultraship shared by both cores, and destroying it between games is the path measured
// as crashing inside driver code (docs/MM_NATIVE.md, claim C057). This core gives back only what is
// its own: threads stopped, config saved, heaps freed. Ship::Context::BeginGameSession then
// installs the incoming game's per-game half.

#include <stdio.h>
#include <stdlib.h>

#include <ship/Context.h>

extern "C" {

// Should the launcher gamestate run at all? This is the ONE place that decides, and graph.c is its
// only caller.
//
// It has to exist because the launcher waits for a human. Every headless tool in the repo --
// harness runs, parity sweeps, screenshot capture -- boots with no one at the keyboard, and a
// chooser that waits for a click would hang all of them. tools/zelda3d_game.py sets
// ZELDA3D_LAUNCHER=0 for exactly that reason.
//
// Default ON, tooling opts OUT, rather than the reverse: that is what makes the launcher the real
// entry point for a person while leaving automation behaving as it did before it existed.
int Zelda3D_LauncherEnabled(void) {
    const char* e = getenv("ZELDA3D_LAUNCHER");
    return !(e != nullptr && e[0] == '0');
}

// Start Majora's Mask: ask the launcher for it, then end this game so the launcher can act.
//
// Both halves are required and neither is enough alone -- the request says WHICH game, and ending
// this one is what gets run() to return. Returns 1 because there is no way for this to fail here:
// whether the MM core can actually be loaded is the launcher's question to answer, and it reports
// its own failures naming the file and the dlopen error.
int Zelda3D_LaunchMM(void) {
    fprintf(stderr, "ZELDA3D LAUNCHER: handing back to the launcher to start Majora's Mask\n");
    fflush(stderr);
    Ship::Context::RequestGameSwitch("mm");
    Ship::Context::RequestExit();
    return 1;
}

// Quit from the launcher. A bare exit(0) skipped the config save and raced the render thread, so
// this asks the frame loop to end and takes the normal window-close shutdown instead: Main_Shutdown
// stops the audio thread, then DeinitOTR persists window layout and config.
//
// Identical to the switch above except for what it does NOT do -- it names no next game, so when
// run() returns the launcher finds nothing pending and the process ends there.
void Zelda3D_LauncherExit(void) {
    fprintf(stderr, "SOH3D LAUNCHER: exit requested\n");
    fflush(stderr);
    Ship::Context::RequestExit();
}

} // extern "C"
