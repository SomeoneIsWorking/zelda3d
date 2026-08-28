#pragma once

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Zelda3DMMPlayerBottleMaterialOverride {
    int enabled;
    int materialIndex;
    int constantIndex;
    float rgba[4];
} Zelda3DMMPlayerBottleMaterialOverride;

// Resolve the complete retail left-hand draw state through one typed adapter.
// The material override is enabled only on the bottle route.
int Zelda3D_MM_PlayerLeftHandDrawState(Player* player, int swordEquipValue, unsigned long long* meshMask,
                                       Zelda3DMMPlayerBottleMaterialOverride* bottleMaterial);

// Convert typed 2S2H Player/save/animation state to retail FUN_00211aa4.
// Returns zero when a required input has no faithful typed equivalent so the
// caller retains the native draw instead of submitting a guessed CMB mask.
int Zelda3D_MM_PlayerLeftHandMeshMask(Player* player, int swordEquipValue, unsigned long long* meshMask);

#ifdef __cplusplus
}
#endif
