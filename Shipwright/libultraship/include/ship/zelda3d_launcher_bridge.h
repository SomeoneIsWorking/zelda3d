#ifndef SHIP_ZELDA3D_LAUNCHER_BRIDGE_H
#define SHIP_ZELDA3D_LAUNCHER_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

extern int gZelda3dLauncherAction;

void Zelda3D_LauncherHitReport(char* out, int outSize);
void Zelda3D_LauncherShow(int show);
int Zelda3D_LauncherIsVisible(void);

#ifdef __cplusplus
}
#endif

#endif // SHIP_ZELDA3D_LAUNCHER_BRIDGE_H
