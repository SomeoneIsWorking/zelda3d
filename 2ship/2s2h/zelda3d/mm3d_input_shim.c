// MM-side definition of the libultraship input-debug hook Zelda3D_DbgInputEnabled().
//
// Shared libultraship (ControlDeck.cpp / KeyboardKeyToButtonMapping.cpp) forward-declares this
// `extern "C" int Zelda3D_DbgInputEnabled(void)` and calls it to gate per-frame input diagnostics.
// It is a one-directional libultraship->engine hook: libultraship never includes engine headers, so
// EACH engine must provide its own definition or the target fails to link. SoH routes it through its
// zelda3d logger registry (Zelda3D_LogEnabled(Z3D_LOG_INPUT), soh/src/zelda3d/input/zelda3d_input.cpp).
// MM has no such logger, so it gates on the ZELDA3D_MM_DBG_INPUT env var (off by default), matching
// the diagnostic intent. Without this file the MM target's link fails on undefined Zelda3D_DbgInputEnabled
// (regression from the input-consolidation pass, which was landed with no MM build available).
#include <stdlib.h>

int Zelda3D_DbgInputEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("ZELDA3D_MM_DBG_INPUT");
        cached = (v != NULL && v[0] != '\0' && v[0] != '0') ? 1 : 0;
    }
    return cached;
}
