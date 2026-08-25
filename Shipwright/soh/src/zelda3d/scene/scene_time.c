#include "scene_time.h"

#include "../render/render_time_control.h"
#include "global.h"

int gZelda3dForceTime = -1;

void Zelda3D_ApplyForceTime(void) {
    Zelda3D_InitForceTime();
    if (gZelda3dForceTime >= 0) {
        gSaveContext.dayTime = (u16)gZelda3dForceTime;
        gSaveContext.skyboxTime = (u16)gZelda3dForceTime;
    }
}
