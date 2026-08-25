// Engine-facing scene collision replacement choke point.
#ifndef ZELDA3D_SCENE_GAMEPLAY_COLLISION_H
#define ZELDA3D_SCENE_GAMEPLAY_COLLISION_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

CollisionHeader* Zelda3D_BuildSceneCollision(PlayState* play, CollisionHeader* n64);
int Zelda3D_CollisionEnabled(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_SCENE_GAMEPLAY_COLLISION_H
