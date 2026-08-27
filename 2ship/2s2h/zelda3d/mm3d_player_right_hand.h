#pragma once

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

// Convert the live typed 2S2H Player fields, model-table pointer, animation
// appearance word, and animation identity to the recovered retail selector.
// Returns zero on an unrecognized input so the caller can retain the native draw.
int Zelda3D_MM_PlayerRightHandMeshMask(const Player* player, unsigned long long* meshMask);

#ifdef __cplusplus
}
#endif
