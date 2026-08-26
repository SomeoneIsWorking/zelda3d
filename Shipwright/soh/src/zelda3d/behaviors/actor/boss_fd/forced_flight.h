// Reproducible Boss_Fd forced-flight control used by paired producer verification.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_FORCED_FLIGHT_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_FORCED_FLIGHT_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_BossFdForceFly(Actor* actor);

// Same forced-flight profile, but seeds the actor's world transform from explicit values so a
// paired oracle can be locked to identical initial conditions before the comparison window.
int Zelda3D_BossFdForceFlySeeded(Actor* actor, const float* pos3, const short* rot3);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_FORCED_FLIGHT_H
