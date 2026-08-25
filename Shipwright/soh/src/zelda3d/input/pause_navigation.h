// Pause-menu navigation state and input injection.
#ifndef ZELDA3D_INPUT_PAUSE_NAVIGATION_H
#define ZELDA3D_INPUT_PAUSE_NAVIGATION_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_PauseNavigationSetTarget(int target);
int Zelda3D_PauseNavigationTarget(void);
void Zelda3D_PauseNavigationInject(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_INPUT_PAUSE_NAVIGATION_H
