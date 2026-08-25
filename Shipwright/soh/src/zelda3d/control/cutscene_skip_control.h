// Runtime cutscene-skip gate and control-taker release.
#ifndef ZELDA3D_CONTROL_CUTSCENE_SKIP_H
#define ZELDA3D_CONTROL_CUTSCENE_SKIP_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int gZelda3dSkip;
int Zelda3D_SkipEnabled(void);
void Zelda3D_SkipControlTakers(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_CONTROL_CUTSCENE_SKIP_H
