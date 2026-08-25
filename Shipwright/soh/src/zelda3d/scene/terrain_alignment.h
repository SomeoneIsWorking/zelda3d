// Queries that reconcile native actor placement with the OoT3D room mesh.
#ifndef ZELDA3D_SCENE_TERRAIN_ALIGNMENT_H
#define ZELDA3D_SCENE_TERRAIN_ALIGNMENT_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef float (*Zelda3D_FloorFn)(float x, float z);
void Zelda3D_ComputeRoomGroundDelta(int modelId, Zelda3D_FloorFn floorFn);
int Zelda3D_RoomGroundDeltaAt(int modelId, float x, float z, float* outDelta);
extern int gZelda3dTerrainWarp;

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_SCENE_TERRAIN_ALIGNMENT_H
