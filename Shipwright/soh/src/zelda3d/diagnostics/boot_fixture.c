#include "boot_fixture.h"

#include "global.h"

#include <stdlib.h>

int Zelda3D_AutoWarpEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char* value = getenv("ZELDA3D_WARP");
        enabled = (value != NULL && value[0] != '\0') ? 1 : 0;
    }
    return enabled;
}

int Zelda3D_AutoWarpEntrance(void) {
    const char* value = getenv("ZELDA3D_ENTRANCE");
    if (value != NULL && value[0] != '\0') {
        return (int)strtol(value, NULL, 0);
    }
    return ENTR_KAKARIKO_VILLAGE_FRONT_GATE;
}

int Zelda3D_ColdBoot(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char* value = getenv("ZELDA3D_COLDBOOT");
        enabled = (value != NULL && value[0] == '1') ? 1 : 0;
    }
    return enabled;
}
