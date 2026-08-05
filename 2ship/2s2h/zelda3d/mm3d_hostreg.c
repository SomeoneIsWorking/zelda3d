// Hand libultraship the functions it needs from the MM game core.
//
// Replaces two files that existed only to satisfy the linker: Z3DSohShim.c (inert definitions of
// OoT3D symbols MM has no use for) and mm3d_input_shim.c. Z3DSohShim.c's own comment named the fix:
// "decouple libultraship from app-defined symbols (weak symbols or a callback-registration seam),
// because libultraship should not hard-reference executable-side state." This is that seam.
//
// It became necessary rather than merely tidy because one binary running both games needs each core
// dlopen'd RTLD_LOCAL -- so OoT's and MM's colliding decomp symbols stay private -- and a core
// loaded that way is invisible to libultraship. See ship/zelda3d_hostiface.h.
//
// MM registers ONE hook. The HUD and measurement hooks belong to the OoT3D layer, which MM does not
// have; leaving them null is the point of the seam, and libultraship no-ops them.

#include "ship/zelda3d_hostiface.h"

#include <stdlib.h>

// Gates per-frame input diagnostics. SoH routes this through its zelda3d logger registry; MM has no
// such logger, so it gates on an env var, off by default, matching the diagnostic intent.
static int Mm3d_DbgInputEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("ZELDA3D_MM_DBG_INPUT");
        cached = (v != NULL && v[0] != '\0' && v[0] != '0') ? 1 : 0;
    }
    return cached;
}

void Mm3d_RegisterHostHooks(void) {
    const Zelda3DGameHooks hooks = {
        Mm3d_DbgInputEnabled, // dbgInputEnabled
        NULL,                 // hudFrame      -- OoT3D native HUD; MM has none
        NULL,                 // hudFlushPoint -- likewise
        NULL,                 // measureResult -- OoT3D auto-scale measurement; MM has none
    };
    Zelda3D_SetGameHooks(&hooks);
}
