// Gold Skulltula draw-local orientation that is absent from Actor::shape.rot.
#ifndef ZELDA3D_RENDER_EN_SW_DRAW_TRANSFORM_H
#define ZELDA3D_RENDER_EN_SW_DRAW_TRANSFORM_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int gZelda3dSwTilt;

void Zelda3D_ApplyEnSwDrawTransform(Actor* actor);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_EN_SW_DRAW_TRANSFORM_H
