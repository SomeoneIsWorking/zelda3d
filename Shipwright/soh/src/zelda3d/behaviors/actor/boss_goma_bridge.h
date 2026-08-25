// C ABI for deterministic Boss Goma climb-state control.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_GOMA_BRIDGE_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_GOMA_BRIDGE_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_BossGomaForceClimb(Actor* actor, float climbY, int hold);
void Zelda3D_BossGomaClimbTick(Actor* actor);
int Zelda3D_BossGomaClimbHeld(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_GOMA_BRIDGE_H
