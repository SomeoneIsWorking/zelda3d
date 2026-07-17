// Z3DBoot.c — see Z3DBoot.h. Pure env readers; no MM state is touched here, so the two decomp
// call sites (title_setup.c routes Setup -> MapSelect; z_select.c auto-loads the entrance) stay
// the only places that know the game types. strtol(base 0) matches zelda3d's ZELDA3D_ENTRANCE parsing
// so a hex entrance ("0x6800") and a decimal one both work.
#include "Z3DBoot.h"

#include <stdlib.h>

int Z3D_AutoWarpEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("ZELDA3D_MM_WARP");
        cached = (v != NULL && v[0] != '\0') ? 1 : 0;
    }
    return cached;
}

int Z3D_AutoWarpEntrance(void) {
    const char* v = getenv("ZELDA3D_MM_ENTRANCE");
    if (v != NULL && v[0] != '\0') {
        return (int)strtol(v, NULL, 0);
    }
    return -1;
}
