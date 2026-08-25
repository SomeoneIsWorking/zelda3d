// Engine-facing Link model-replacement choke point and feature gate.
#ifndef ZELDA3D_PLAYER_DRAW_BRIDGE_H
#define ZELDA3D_PLAYER_DRAW_BRIDGE_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_TryDrawPlayer(PlayState* play, Actor* actor);
extern int gZelda3dLinkOn;
int Zelda3D_LinkEnabled(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_PLAYER_DRAW_BRIDGE_H
