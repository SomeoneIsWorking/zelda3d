// How the launcher process starts a game, without either side naming the other's symbols.
//
// A game core (OoT, MM) is a shared object the launcher dlopen's with RTLD_NOW | RTLD_LOCAL.
// RTLD_LOCAL is the whole point: OoT and MM each define Play_Init, Actor_Draw and 6,614 more
// colliding decomp symbols (claim C050), and RTLD_LOCAL keeps a core's symbols out of the global
// namespace so two cores can be loaded at once without fighting. The cost of that invisibility is
// that the launcher cannot link against ANY core symbol -- so the entire surface is the one export
// below, found by dlsym on a string, and everything else travels through the returned struct.
//
// This is the other half of zelda3d_hostiface.h. That header inverts libultraship -> game calls into
// registered hooks; this one is launcher -> game. Both exist for the same reason: a core is loaded,
// not linked, and only libultraship is shared by name.
//
// A core links against libultraship, so engine state is genuinely shared -- one window, one
// renderer, one resource manager (claim C054). Only game code is duplicated, and duplicate game
// code is exactly what we want private.

#ifndef SHIP_ZELDA3D_CORE_H
#define SHIP_ZELDA3D_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

// Bump when the struct below changes shape. The launcher REFUSES a mismatch rather than reading a
// struct laid out differently than it expects -- a stale core .so left in a build dir is otherwise
// an unexplained crash inside a function pointer.
#define ZELDA3D_CORE_ABI 1

// What a core tells the launcher about itself, and the one thing it lets the launcher do.
typedef struct Zelda3DCore {
    int abi;           // must equal ZELDA3D_CORE_ABI
    const char* id;    // stable machine name: "oot", "mm"
    const char* title; // human name for the launcher UI

    // Run the game to completion on the CALLING thread; returns the process exit code. This is the
    // former int main() of the standalone binary, so it owns InitOTR/heaps/frame loop/teardown.
    int (*run)(int argc, char** argv);
} Zelda3DCore;

// Every core exports exactly this, under exactly this name. Returns a pointer to a static
// descriptor -- valid as long as the core stays loaded, and never freed by the caller.
#define ZELDA3D_CORE_ENTRY_SYMBOL "Zelda3D_CoreEntry"
typedef const Zelda3DCore* (*Zelda3DCoreEntryFn)(void);

#ifdef __cplusplus
}
#endif

#endif // SHIP_ZELDA3D_CORE_H
