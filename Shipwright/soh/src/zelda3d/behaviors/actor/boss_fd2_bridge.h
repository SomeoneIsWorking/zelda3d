// C ABI between Boss_Fd2's native actor and its Zelda3D behavior module.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_BRIDGE_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_BRIDGE_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_BossFd2DrawManeSegment(PlayState* play, Actor* actor, int chain, int segment, const Vec3f* pos,
                                   const Vec3f* rot, const Vec3f* scale);
// Applies the posed CMB limb-14 roots. Returns 1 when available, 0 for the native fallback.
int Zelda3D_BossFd2PrepareRenderedMane(Actor* actor);
int Zelda3D_BossFd2ForceGround(Actor* actor);
int Zelda3D_BossFd2ForceDamageState(PlayState* play, Actor* actor, int state);
void Zelda3D_BossFd2IdleTick(PlayState* play, Actor* actor);
int Zelda3D_BossFd2ResolveAnim(PlayState* play, Actor* actor, const char** outCsab, float* outFrame,
                               const char** outMorphCsab, float* outMorphFrame, float* outMorphWeight);
int Zelda3D_BossFd2CaptureAnimController(Actor* actor);
int Zelda3D_BossFd2RestoreAnimController(Actor* actor);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_BRIDGE_H
