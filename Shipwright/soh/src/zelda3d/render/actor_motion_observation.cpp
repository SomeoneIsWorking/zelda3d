#include "actor_motion_observation.h"

FILE* sZelda3dMotionFile = NULL;
Actor* sZelda3dMotionActor = NULL;
s32 sZelda3dMotionRemaining = 0;
s32 sZelda3dMotionFrame = 0;

void Zelda3D_ActorMotionObservationResetRunState(void) {
    sZelda3dMotionActor = NULL;
}
