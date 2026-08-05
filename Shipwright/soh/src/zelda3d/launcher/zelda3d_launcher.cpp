// OoT/MM launcher — the process side.
//
// The RmlUi layer (libultraship SohRmlUi) owns the launcher DOCUMENT and records which game was
// chosen in gZelda3dLauncherAction. It deliberately knows nothing about processes. This module is
// the other half: turning "Majora's Mask" into a running MM.
//
// WHY THIS STILL EXECS. One process running both games is no longer hypothetical: Shipwright/
// zelda3d_app is a launcher binary that dlopens either game core with RTLD_LOCAL, and both OoT and
// MM have been verified reaching gameplay that way. What this module does is the OTHER entry path
// -- soh.elf started directly, with OoT already booted and its chooser presented as a gamestate.
//
// Switching games from HERE cannot be a core swap, because this process is the OoT core: handing
// control to MM would mean tearing down a live Ship::Context, window and renderer mid-frame and
// rebuilding them for another game. That teardown is the unfinished half of N3 in
// docs/MM_NATIVE.md, and exec sidesteps it by letting the kernel do the cleanup.
//
// So there are deliberately two paths, and the difference is which one the user started:
//   zelda3d (the launcher)  -- picks a core BEFORE any engine exists, no teardown needed
//   soh.elf (this)          -- OoT is already running, so the chooser can only exec
// The right end state is the RmlUi document moving into the launcher process, at which point this
// file goes away. Until the Context handover exists, deleting it would remove a working chooser and
// replace it with nothing.
//
// The exec'd process INHERITS this one's environment, so ZELDA3D_MM_ROM / headless settings / the
// display carry over with no extra plumbing.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <ship/Context.h>
#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#include <libgen.h>
#endif

extern "C" {

// Should the launcher gamestate run at all? This is the ONE place that decides, and graph.c is its
// only caller.
//
// It has to exist because the launcher waits for a human. Every headless tool in the repo --
// harness runs, parity sweeps, screenshot capture -- boots with no one at the keyboard, and a
// chooser that waits for a click would hang all of them. tools/zelda3d_game.sh sets
// ZELDA3D_LAUNCHER=0 for exactly that reason.
//
// Default ON, tooling opts OUT, rather than the reverse: that is what makes the launcher the real
// entry point for a person while leaving automation behaving as it did before it existed.
int Zelda3D_LauncherEnabled(void) {
    const char* e = getenv("ZELDA3D_LAUNCHER");
    return !(e != nullptr && e[0] == '0');
}

// Resolve mm.elf next to this executable, which is where the build puts it (build-cmake/mm/mm.elf
// beside build-cmake/soh/soh.elf), unless ZELDA3D_MM overrides it — the same override tools/mm_game.sh
// already honours, so a sibling build dir keeps working.
static int Zelda3D_ResolveMMPath(char* out, size_t outSize) {
#if defined(__linux__)
    if (const char* env = getenv("ZELDA3D_MM"); env != nullptr && env[0] != '\0') {
        snprintf(out, outSize, "%s", env);
        return access(out, X_OK) == 0;
    }
    char self[4096];
    const ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) {
        return 0;
    }
    self[n] = '\0';
    // .../soh/soh.elf -> .../mm/mm.elf
    char* dir = dirname(self);          // .../soh
    char* parent = dirname(dir);        // ...
    snprintf(out, outSize, "%s/mm/mm.elf", parent);
    return access(out, X_OK) == 0;
#else
    (void)out;
    (void)outSize;
    return 0; // only the Linux dev path is wired; see the message in Zelda3D_LaunchMM
#endif
}

// Replace this process with MM. Returns only on FAILURE — and says why, loudly, rather than
// silently returning to an OoT title screen the user did not ask for. A launcher whose "Majora's
// Mask" button appears to do nothing is worse than one that reports it cannot find the binary.
void Zelda3D_LaunchMM(void) {
#if defined(__linux__)
    char path[4096];
    if (!Zelda3D_ResolveMMPath(path, sizeof path)) {
        fprintf(stderr,
                "SOH3D LAUNCHER: cannot start Majora's Mask -- no executable mm.elf found.\n"
                "  Looked next to this binary (../mm/mm.elf) and at $ZELDA3D_MM.\n"
                "  Build it with: cmake --build Shipwright/build-cmake --target mm\n");
        fflush(stderr);
        return;
    }
    fprintf(stderr, "SOH3D LAUNCHER: starting Majora's Mask -> %s\n", path);
    fflush(stderr);
    char* const argv[] = { path, nullptr };
    execv(path, argv);
    // execv only returns on failure.
    fprintf(stderr, "SOH3D LAUNCHER: execv(%s) FAILED: %s\n", path, strerror(errno));
    fflush(stderr);
#else
    fprintf(stderr, "SOH3D LAUNCHER: starting MM from the launcher is only wired for Linux so far; "
                    "run the mm binary directly.\n");
    fflush(stderr);
#endif
}

// Quit from the launcher. A bare exit(0) skipped the config save and raced the render thread, so
// this asks the frame loop to end and takes the normal window-close shutdown instead: Main_Shutdown
// stops the audio thread, then DeinitOTR persists window layout and config.
//
// The process still ends here rather than returning -- DeinitOTR finishes with a deliberate
// _exit(0). That is orderly, not graceful, and the distinction matters for the in-process game
// switch this file's header describes: see docs/MM_NATIVE.md N3.
void Zelda3D_LauncherExit(void) {
    fprintf(stderr, "SOH3D LAUNCHER: exit requested\n");
    fflush(stderr);
    Ship::Context::RequestExit();
}

} // extern "C"
