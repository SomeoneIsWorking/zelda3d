// Boss_Fd2's shared ten-point mane-chain simulation and draw submission.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_MANE_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_MANE_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_BossFd2UpdateMane(Actor* actor, PlayState* play, s16 chain, Vec3f* head, Vec3f* pos, Vec3f* rot,
                               Vec3f* pull, f32* scale, s16 substeps);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_MANE_H
