// Public C ABI for selecting and transitioning between the two game cores.
#ifndef ZELDA3D_LAUNCHER_LAUNCHER_H
#define ZELDA3D_LAUNCHER_LAUNCHER_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

void Launcher_Init(GameState* gameState);
void Launcher_Main(GameState* gameState);
void Launcher_Destroy(GameState* gameState);
int Zelda3D_LauncherEnabled(void);
void Zelda3D_LauncherShow(int show);
int Zelda3D_LauncherIsVisible(void);
int Zelda3D_LaunchMM(void);
void Zelda3D_LauncherExit(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_LAUNCHER_LAUNCHER_H
