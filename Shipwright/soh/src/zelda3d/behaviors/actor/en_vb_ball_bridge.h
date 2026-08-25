// C ABI for the OoT3D En_Vb_Ball behavior and its deterministic spawn fixture.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_EN_VB_BALL_BRIDGE_H
#define ZELDA3D_BEHAVIORS_ACTOR_EN_VB_BALL_BRIDGE_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

Actor* Zelda3D_EnVbBallSpawnDiagnostic(PlayState* play, Actor* parent, int params);
int Zelda3D_EnVbBallUpdateShadow(Actor* actor);
int Zelda3D_EnVbBallPrepareBoneBounce(Actor* actor);
int Zelda3D_EnVbBallSpawnImpactEffects(PlayState* play, Actor* actor);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_ACTOR_EN_VB_BALL_BRIDGE_H
