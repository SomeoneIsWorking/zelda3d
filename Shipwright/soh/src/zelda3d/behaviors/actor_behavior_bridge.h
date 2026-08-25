// C ABI for dispatching live actors through the structured Zelda3D behavior registry.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BEHAVIOR_BRIDGE_H
#define ZELDA3D_BEHAVIORS_ACTOR_BEHAVIOR_BRIDGE_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_TryActorModelDraw(PlayState* play, Actor* actor);
int Zelda3D_TryActorDeferredDraw(PlayState* play, Actor* actor);
void Zelda3D_ActorBehaviorPreUpdate(PlayState* play, Actor* actor);
void Zelda3D_ActorBehaviorPostUpdate(PlayState* play, Actor* actor);
int Zelda3D_ActorDrawSpaceTransform(void* actor, float* outLiftY, float* outLocalOffset);
int Zelda3D_ActorHasBehaviorModule(s16 actorId);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_ACTOR_BEHAVIOR_BRIDGE_H
