// Engine-facing C ABI for the generic actor replacement choke point.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_DRAW_H
#define ZELDA3D_BEHAVIORS_ACTOR_DRAW_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_TryDrawActor(PlayState* play, Actor* actor);
int Zelda3D_ActorHasReplacement(PlayState* play, Actor* actor);
void Zelda3D_ActorPostUpdate(PlayState* play, Actor* actor);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_ACTOR_DRAW_H
