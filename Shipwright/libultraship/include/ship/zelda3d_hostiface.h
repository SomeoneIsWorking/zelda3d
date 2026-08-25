// The seam between the shared engine (libultraship) and a game core.
//
// libultraship used to name six game symbols directly -- four functions it calls and two ints it
// reads and writes. That works when the game is the executable, because an executable's symbols are
// visible to the libraries it loads. It does NOT work for one binary running both games: each core
// has to be dlopen'd with RTLD_LOCAL so that OoT's and MM's 6,616 colliding decomp symbols stay
// private, and RTLD_LOCAL means the core is invisible to everything else in the process. That
// invisibility is the feature -- it is what lets two cores each define Play_Init -- so it cannot be
// switched off for the handful of symbols libultraship happens to want.
//
// So the dependency is inverted here, in the two ways the six divide into:
//
//   DATA (gZelda3dInputDevice, gZelda3dHlGroup) -- libultraship OWNS them now. A core links against
//   libultraship, so a core referencing them resolves normally, in the direction that works. No API
//   was needed; only the definition moved.
//
//   FUNCTIONS -- the implementation genuinely lives in game code, so the core hands libultraship
//   pointers at startup via Zelda3D_SetGameHooks and libultraship calls through the Host* wrappers.
//   Unregistered is a valid state, not an error: a core that has no OoT3D layer simply never
//   registers and the wrappers are no-ops. That replaces 2ship/2s2h/Z3DSohShim.c, which existed
//   only to define these symbols so MM would link, and whose own comment named this seam as the
//   proper fix.
//
// Anything added here is a new coupling between engine and game -- prefer an existing Ship:: API.

#ifndef SHIP_ZELDA3D_HOSTIFACE_H
#define SHIP_ZELDA3D_HOSTIFACE_H

#ifdef __cplusplus
extern "C" {
#endif

// Last-used input device: 0 = gamepad (Xbox glyphs), 1 = keyboard (key-label glyphs). Always one of
// those two -- there is no "unset" state. Written by the input layer here on real device events; the
// game's HUD reads it each frame to pick a glyph set. A core that wants to seed it from
// configuration does so explicitly at startup, before any event can arrive.
extern int gZelda3dInputDevice;

// Room-group highlight index for the renderer, -1 = disabled (REPL `hlroom`).
extern int gZelda3dHlGroup;

// Implemented by a game core; every field may be NULL.
typedef struct Zelda3DGameHooks {
    int (*dbgInputEnabled)(void);              // is the debug input path active?
    void (*hudFrame)(void);                    // per-frame native HUD entry, from Gui::EndFrame
    void (*hudFlushPoint)(void);               // HUD batch flush boundary, from the Fast3D interpreter
    void (*measureResult)(int key, float height, float footprintX,
                          float footprintZ); // report measured model extents back to the game
} Zelda3DGameHooks;

// Install a core's hooks. Pass NULL to clear (a core being unloaded MUST clear, or libultraship
// keeps pointers into an unmapped .so). Copies the struct; the caller need not keep it alive.
void Zelda3D_SetGameHooks(const Zelda3DGameHooks* hooks);

// What libultraship calls. Each is a no-op returning a neutral value when unregistered.
int Zelda3D_HostDbgInputEnabled(void);
void Zelda3D_HostHudFrame(void);
void Zelda3D_HostHudFlushPoint(void);
void Zelda3D_HostMeasureResult(int key, float height, float footprintX, float footprintZ);

#ifdef __cplusplus
}
#endif

#endif // SHIP_ZELDA3D_HOSTIFACE_H
