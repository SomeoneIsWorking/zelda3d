// Ordered actor draw routing across behavior, explicit, and automatic replacements.
#ifndef ZELDA3D_RENDER_ACTOR_DRAW_DISPATCH_H
#define ZELDA3D_RENDER_ACTOR_DRAW_DISPATCH_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_TryDrawActor(PlayState* play, Actor* actor);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_ACTOR_DRAW_DISPATCH_H
