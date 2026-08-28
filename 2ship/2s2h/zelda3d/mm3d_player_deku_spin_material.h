#pragma once

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Zelda3DMMPlayerDekuSpinMaterialOverride {
    int enabled;
    int materialIndex;
    int constantIndex;
    float rgba[4];
} Zelda3DMMPlayerDekuSpinMaterialOverride;

// Adapt typed 2S2H Player form/action/phase state to retail Player_Draw's
// Deku spin material write. Returns zero only for an invalid input state.
int Zelda3D_MM_PlayerDekuSpinMaterialOverride(const Player* player,
                                              Zelda3DMMPlayerDekuSpinMaterialOverride* materialOverride);

#ifdef __cplusplus
}
#endif
