#include "actor_selection.h"

#include "../render/actor_control_state.h"

Actor* gZelda3dSelActor = NULL;
Actor* gZelda3dHideActor = NULL;
Actor* gZelda3dZTargetActor = NULL;
s32 gZelda3dSelId = -1;
s32 gZelda3dActorFreeze = 0;

void Zelda3D_ActorSelectionPostUpdate(Actor* actor) {
    if (actor == NULL || actor != gZelda3dSelActor || !gZelda3dActorFreeze) {
        return;
    }
    actor->velocity.x = actor->velocity.y = actor->velocity.z = 0.0f;
    actor->speedXZ = 0.0f;
    actor->world.pos = sZelda3dActorPinPos;
    if (gZelda3dActorFreeze != 2) {
        actor->shape.rot = actor->world.rot = sZelda3dActorPinRot;
    }
}

void Zelda3D_ActorSelectionResetRunState(void) {
    gZelda3dSelActor = NULL;
    gZelda3dHideActor = NULL;
    gZelda3dZTargetActor = NULL;
}
