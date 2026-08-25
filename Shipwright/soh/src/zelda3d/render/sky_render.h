// OoT3D sky dome, cloud layer, and star-field replacement.
#ifndef ZELDA3D_RENDER_SKY_H
#define ZELDA3D_RENDER_SKY_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_TryDrawSky(PlayState* play);
int Zelda3D_SkyActive(PlayState* play);
int Zelda3D_SkyModelId(int index);
int Zelda3D_ActiveSkyIndex(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_SKY_H
