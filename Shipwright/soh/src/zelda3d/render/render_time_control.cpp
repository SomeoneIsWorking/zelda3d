#include "render_time_control.h"
#include "../scene/scene_time.h"

#include <cstdlib>
void Zelda3D_InitForceTime(void) {
    static int done = 0;
    const char* v;
    if (done) {
        return;
    }
    done = 1;
    v = getenv("ZELDA3D_TIME");
    if (v != NULL && v[0] != '\0') {
        gZelda3dForceTime = (int)strtol(v, NULL, 0); // 0 base: accepts 0x.. hex or decimal
    }
}
