// Logic-frame freeze state used by deterministic frame stepping.
#ifndef ZELDA3D_CONTROL_FRAME_STEP_H
#define ZELDA3D_CONTROL_FRAME_STEP_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int gZelda3dFreeze;
void Play_Update(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_CONTROL_FRAME_STEP_H
