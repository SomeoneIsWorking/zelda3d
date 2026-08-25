// Hand libultraship the four functions it needs from this game core.
//
// libultraship used to call Zelda3D_HudFrame, Zelda3D_HudFlushPoint, Zelda3D_MeasureResult and
// Zelda3D_DbgInputEnabled by NAME. That resolves only while the game is the executable, whose
// symbols are visible to the libraries it loads. One binary running both games cannot work that
// way: each core must be dlopen'd RTLD_LOCAL so OoT's and MM's colliding decomp symbols stay
// private, and RTLD_LOCAL makes the core invisible to libultraship. See
// libultraship/include/ship/zelda3d_hostiface.h for the full reasoning.
//
// So the core registers instead. This runs from InitOTR, before any frame is drawn and before the
// input layer can fire -- the hooks are read by the render and input paths, and neither exists yet
// at that point.

#include "ship/zelda3d_hostiface.h"

#include "zelda3d/hud/zelda3d_hud.h"
#include "zelda3d/input/zelda3d_input.h"
#include "zelda3d/render/replacement_calibration.h"

extern "C" void Zelda3D_RegisterHostHooks(void) {
    // Seed engine-owned state from this core's configuration before anything can write it. Doing it
    // here rather than lazily on first read is what keeps ZELDA3D_INPUTDEV from losing a race with
    // an early device event -- see Zelda3D_InputDeviceInit.
    Zelda3D_InputDeviceInit();

    const Zelda3DGameHooks hooks = {
        /* dbgInputEnabled */ Zelda3D_DbgInputEnabled,
        /* hudFrame        */ Zelda3D_HudFrame,
        /* hudFlushPoint   */ Zelda3D_HudFlushPoint,
        /* measureResult   */ Zelda3D_MeasureResult,
    };
    Zelda3D_SetGameHooks(&hooks);
}
