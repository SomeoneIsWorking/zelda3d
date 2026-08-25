#include "zelda3d_runtime.h"

#include <stdlib.h>

int gZelda3dEnabled = -1;

int Zelda3D_Enabled(void) {
    if (gZelda3dEnabled < 0) {
        const char* value = getenv("SOH3D");
        gZelda3dEnabled = (value != NULL && value[0] == '0') ? 0 : 1;
    }
    return gZelda3dEnabled;
}
