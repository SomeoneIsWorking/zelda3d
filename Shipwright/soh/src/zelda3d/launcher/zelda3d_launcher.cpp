// OoT/MM launcher — the process side.
//
// The RmlUi layer (libultraship SohRmlUi) owns the launcher DOCUMENT and records which game was
// chosen in gZelda3dLauncherAction. It deliberately knows nothing about processes. This module is
// the other half: turning "Majora's Mask" into a running MM.
//
// WHY EXEC AND NOT ONE PROCESS. OoT is this executable (soh.elf); MM is a separate native 2S2H
// build (mm.elf) with its own Ship::Context, ResourceManager and archive set. Running both in one
// process is milestone N3 in docs/MM_NATIVE.md and needs context ownership, per-game resource
// registries and .otr/.o2r multiplexing untangled first. Replacing this process with mm.elf gets a
// working chooser now without pretending that work is done, and it keeps the launcher as the single
// entry point rather than spawning a second window.
//
// The exec'd process INHERITS this one's environment, so ZELDA3D_MM_ROM / headless settings / the
// display carry over with no extra plumbing.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
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

// Quit from the launcher. exit() rather than a graceful window teardown would skip the config save
// and race the render thread, so ask the window to close and let the normal shutdown path run.
void Zelda3D_LauncherExit(void) {
    fprintf(stderr, "SOH3D LAUNCHER: exit requested\n");
    fflush(stderr);
    exit(0);
}

} // extern "C"
