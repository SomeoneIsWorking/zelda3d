// Boss_Fd diagnostic effect-population override consumed by the 3DS particle renderer.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_EFFECT_OVERRIDE_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_EFFECT_OVERRIDE_H

#include "global.h"

struct BossFd;

namespace Zelda3D::BossFdEffects {

void applyOverride(BossFd* boss);

} // namespace Zelda3D::BossFdEffects

extern "C" int Zelda3D_BossFdForceEffects(Actor* actor, int type3ds, int count);

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_EFFECT_OVERRIDE_H
