// OoT3D Camera_CalcAtDefault Y-bias producer and consumer seam.
#ifndef ZELDA3D_BEHAVIORS_CAMERA_AT_DEFAULT_H
#define ZELDA3D_BEHAVIORS_CAMERA_AT_DEFAULT_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Advance the OoT3D-only Y-bias producer. Returns nonzero when Zelda3D owns Player::unk_6C4 for
// this update; zero leaves the stock slope path in control. The action flags must be typed
// `Player_Action_80842180` (walk/run) and `Player_Action_8084E6D4` (get-item) comparisons from
// z_player.c; the latter is OoT3D's explicit exception on slope floor types 4, 7, and 12.
int Zelda3D_CameraAtDefaultUpdatePlayer(PlayState* play, Player* player, s32 floorType, s32 isWalkRunAction,
                                        s32 isGetItemAction);

// Return the extra Camera_CalcAtDefault at.y term. This is non-inserting: a player with no active
// producer state reads as zero.
f32 Zelda3D_CameraAtDefaultYBias(const Player* player);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_CAMERA_AT_DEFAULT_H
