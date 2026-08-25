#ifndef ZELDA3D_RENDER_GET_ITEM_RENDER_H
#define ZELDA3D_RENDER_GET_ITEM_RENDER_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_TryDrawGetItem(PlayState* play, s16 drawId);

extern float gZelda3dGiScaleMul;
extern float gZelda3dGiRotX;
extern float gZelda3dGiRotY;
extern float gZelda3dGiRotZ;

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_GET_ITEM_RENDER_H
