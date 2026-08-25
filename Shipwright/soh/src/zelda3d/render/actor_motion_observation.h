// Run-scoped actor motion-sample sink used by REPL diagnostics.
#ifndef ZELDA3D_RENDER_ACTOR_MOTION_OBSERVATION_H
#define ZELDA3D_RENDER_ACTOR_MOTION_OBSERVATION_H

#include "global.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern FILE* sZelda3dMotionFile;
extern Actor* sZelda3dMotionActor;
extern s32 sZelda3dMotionRemaining;
extern s32 sZelda3dMotionFrame;
void Zelda3D_ActorMotionObservationResetRunState(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_ACTOR_MOTION_OBSERVATION_H
