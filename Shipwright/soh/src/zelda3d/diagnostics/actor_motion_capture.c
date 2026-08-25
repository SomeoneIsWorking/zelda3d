#include "actor_motion_capture.h"

#include "../render/actor_motion_observation.h"

void Zelda3D_ActorMotionCapturePostUpdate(PlayState* play, Actor* actor) {
    if (sZelda3dMotionFile == NULL || actor != sZelda3dMotionActor || sZelda3dMotionRemaining <= 0) {
        return;
    }
    fprintf(sZelda3dMotionFile, "%d,%u,0x%X,%.3f,%.3f,%.3f,%d,%d,%d,%.4f,%.4f,%.4f,%.4f\n", sZelda3dMotionFrame,
            play->gameplayFrames, actor->id, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z,
            actor->world.rot.x, actor->world.rot.y, actor->world.rot.z, actor->velocity.x, actor->velocity.y,
            actor->velocity.z, actor->speedXZ);
    fflush(sZelda3dMotionFile);
    sZelda3dMotionFrame++;
    if (--sZelda3dMotionRemaining <= 0) {
        fclose(sZelda3dMotionFile);
        sZelda3dMotionFile = NULL;
        sZelda3dMotionActor = NULL;
    }
}
