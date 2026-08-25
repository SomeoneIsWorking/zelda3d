#include "ship/zelda3d_hostiface.h"

#include <cstddef>

// See the header for why this seam exists. The short version: RTLD_LOCAL makes a game core
// invisible to the process, so libultraship cannot name a game symbol and expect it to resolve.

extern "C" {

// Owned here now rather than by whichever game happened to be the executable.
//
// 1 (keyboard) is a real default, not a sentinel. This used to be -1 meaning "unresolved", with a
// getter that read ZELDA3D_INPUTDEV on first call -- but the input layer below WRITES this variable
// directly on device events, so any keypress arriving before the first HUD draw moved it off -1 and
// the environment variable was then never consulted at all. The override silently lost a race. A
// core resolves the environment once, explicitly, at startup instead (Zelda3D_InputDeviceInit).
int gZelda3dInputDevice = 1;
int gZelda3dHlGroup = -1;

} // extern "C"

namespace {
// Not atomic, and deliberately so: hooks are installed once during core startup, before the render
// and input threads that read them exist. Making this atomic would imply a core may swap hooks
// under a running frame, which is not a thing we want to quietly support -- unload, then load.
Zelda3DGameHooks sHooks = {};
} // namespace

extern "C" void Zelda3D_SetGameHooks(const Zelda3DGameHooks* hooks) {
    if (hooks == nullptr) {
        sHooks = Zelda3DGameHooks{};
        return;
    }
    sHooks = *hooks;
}

// Unregistered returns 0 = "debug input off", the same answer the pre-seam build gave for a game
// with no OoT3D layer. A neutral default, not a swallowed error: having no hooks is a valid state
// (see the header), so there is nothing here to report.
extern "C" int Zelda3D_HostDbgInputEnabled(void) {
    return sHooks.dbgInputEnabled != nullptr ? sHooks.dbgInputEnabled() : 0;
}

extern "C" void Zelda3D_HostHudFrame(void) {
    if (sHooks.hudFrame != nullptr) {
        sHooks.hudFrame();
    }
}

extern "C" void Zelda3D_HostHudFlushPoint(void) {
    if (sHooks.hudFlushPoint != nullptr) {
        sHooks.hudFlushPoint();
    }
}

extern "C" void Zelda3D_HostMeasureResult(int key, float height, float footprintX, float footprintZ) {
    if (sHooks.measureResult != nullptr) {
        sHooks.measureResult(key, height, footprintX, footprintZ);
    }
}
