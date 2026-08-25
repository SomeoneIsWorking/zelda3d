// Scene fog conversion and renderer-state submission.
#ifndef ZELDA3D_RENDER_FOG_RENDER_H
#define ZELDA3D_RENDER_FOG_RENDER_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_FogSetPosition(float minimum, float maximum);
void Zelda3D_UpdateFog(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_FOG_RENDER_H
