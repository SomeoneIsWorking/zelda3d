// Public diagnostic seam for measuring Link's posed ground offset.
#ifndef ZELDA3D_PLAYER_GROUND_DIAGNOSTICS_H
#define ZELDA3D_PLAYER_GROUND_DIAGNOSTICS_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

float Zelda3D_LinkGroundDiag(PlayState* play, const char** outCsab);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_PLAYER_GROUND_DIAGNOSTICS_H
