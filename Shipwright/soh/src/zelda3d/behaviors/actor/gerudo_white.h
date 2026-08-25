// Zelda3D En_Ge1 animation bridge: maps the live N64 animation and joint table to the OoT3D rig.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_GERUDO_WHITE_H
#define ZELDA3D_BEHAVIORS_ACTOR_GERUDO_WHITE_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

const char* Zelda3D_ResolveAnim_EnGe1(Actor* actor);
int Zelda3D_Joints_EnGe1(Actor* actor, const s16** outJointRots, int* outLimbCount);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_ACTOR_GERUDO_WHITE_H
