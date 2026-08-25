// Actor render-height reconciliation against OoT3D room terrain.
#ifndef ZELDA3D_RENDER_TERRAIN_ALIGNMENT_H
#define ZELDA3D_RENDER_TERRAIN_ALIGNMENT_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

float Zelda3D_N64FloorCb(float x, float z);
float Zelda3D_RenderYOffsetAtXZ(PlayState* play, Actor* actor, float x, float z);
float Zelda3D_ActorRenderYOffset(PlayState* play, Actor* actor);
void Zelda3D_TerrainAlignmentResetRunState(void);

extern PlayState* sWarpPlay;

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_TERRAIN_ALIGNMENT_H
