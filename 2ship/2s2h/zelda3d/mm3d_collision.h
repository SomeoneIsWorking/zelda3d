// mm3d_collision — drive MM gameplay collision from the MM3D scene-collision mesh.
//
// WHY: MM renders MM3D room geometry (mm3d_draw.c) but, until this, still COLLIDED against the N64
// mesh. Two independent sources for "where the ground is" means the player floats or clips anywhere
// the 3DS remodel moved a surface — measured in South Clock Town, where Link rested at y=25.4 while
// the 3DS room mesh had a single surface at y=0.0 under him. One geometry for visuals AND gameplay
// is the fix, mirroring what OoT already does (soh/src/zelda3d/scene/zelda3d_collision.cpp).
#ifndef ZELDA3D_MM3D_COLLISION_H
#define ZELDA3D_MM3D_COLLISION_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

// Build an MM3D-backed CollisionHeader for the current scene, or NULL to fall through to the N64
// header. `n64` is the scene's own header — its water boxes and bg-camera list are carried over,
// since those are gameplay metadata the MM3D collision file expresses differently.
//
// The returned header and its arrays are owned by this module and freed on the next scene build.
CollisionHeader* Zelda3D_MM_BuildSceneCollision(PlayState* play, CollisionHeader* n64);

// Non-zero while the collision header installed in colCtx is the MM3D one. BgCheck's per-scene
// subdivision / node-pool constants are hand-tuned to the N64 mesh and MUST NOT be applied to it.
int Zelda3D_MM_CollisionDiverted(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_MM3D_COLLISION_H
