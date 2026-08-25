// Per-frame CSV capture of one selected actor's native motion state.
#ifndef ZELDA3D_DIAGNOSTICS_ACTOR_MOTION_CAPTURE_H
#define ZELDA3D_DIAGNOSTICS_ACTOR_MOTION_CAPTURE_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_ActorMotionCapturePostUpdate(PlayState* play, Actor* actor);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_DIAGNOSTICS_ACTOR_MOTION_CAPTURE_H
