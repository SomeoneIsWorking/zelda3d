// Logic-frame freeze state used by deterministic frame stepping.
#ifndef ZELDA3D_CONTROL_FRAME_STEP_H
#define ZELDA3D_CONTROL_FRAME_STEP_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int gZelda3dFreeze;
void Play_Update(PlayState* play);

// Advance exactly one logic frame: inject the held REPL input, then update. Every REPL command that
// steps frames under `freeze` goes through this, so a per-frame trace and `step N` see the same frame
// boundary.
void Zelda3D_StepLogicFrame(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_CONTROL_FRAME_STEP_H
