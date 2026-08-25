#include "player_grab_driver.h"

#include "../diagnostics/actor_selection.h"

#include <cmath>

void Zelda3D::LinkGrabDriver::start(int frames) {
    mFrames = frames;
}

void Zelda3D::LinkGrabDriver::walkInject(PlayState* play) {
    if (mFrames <= 0) {
        return;
    }

    Player* player = GET_PLAYER(play);
    if (player == nullptr || gZelda3dSelActor == nullptr || player->heldActor != nullptr) {
        mFrames = 0;
        return;
    }

    const float yawRadians = player->actor.shape.rot.y * (3.14159265f / 32768.0f);
    const float forwardX = sinf(yawRadians);
    const float forwardZ = cosf(yawRadians);
    constexpr float kGrabDistance = 26.0f;
    gZelda3dSelActor->world.pos.x = player->actor.world.pos.x + forwardX * kGrabDistance;
    gZelda3dSelActor->world.pos.y = player->actor.world.pos.y + 4.0f;
    gZelda3dSelActor->world.pos.z = player->actor.world.pos.z + forwardZ * kGrabDistance;
    gZelda3dSelActor->speedXZ = 0.0f;
    gZelda3dSelActor->velocity.y = 0.0f;
    play->state.input[0].cur.button |= BTN_A;
    if ((mFrames & 3) == 0) {
        play->state.input[0].press.button |= BTN_A;
    }
    --mFrames;
}
