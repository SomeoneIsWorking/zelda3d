#pragma once

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

// Convert typed 2S2H Player/save/animation state to retail FUN_00211aa4.
// Returns zero when a required input has no faithful typed equivalent so the
// caller retains the native draw instead of submitting a guessed CMB mask.
int Zelda3D_MM_PlayerLeftHandMeshMask(Player* player, int swordEquipValue, unsigned long long* meshMask);

#ifdef __cplusplus
}
#endif
