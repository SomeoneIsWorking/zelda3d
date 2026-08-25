// Sun, moon, and title-atmosphere replacement draws.
#ifndef ZELDA3D_RENDER_CELESTIAL_H
#define ZELDA3D_RENDER_CELESTIAL_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_TryDrawSunMoon(PlayState* play);
int Zelda3D_TryDrawTitleAtmos(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_CELESTIAL_H
