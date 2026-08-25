#include "player_transform_pin.h"

void Zelda3D::LinkTransformPin::set(const Player* player, bool enabled) {
    mEnabled = enabled;
    if (!mEnabled || player == nullptr) {
        return;
    }
    mPosition = player->actor.world.pos;
    mYaw = player->actor.shape.rot.y;
}

void Zelda3D::LinkTransformPin::apply(PlayState* play, Actor* actor) const {
    if (actor == nullptr || !mEnabled || play == nullptr || actor != &GET_PLAYER(play)->actor) {
        return;
    }

    actor->velocity.x = actor->velocity.y = actor->velocity.z = 0.0f;
    actor->speedXZ = 0.0f;
    actor->world.pos = mPosition;
    actor->shape.rot.y = actor->world.rot.y = mYaw;
}

bool Zelda3D::LinkTransformPin::enabled() const {
    return mEnabled;
}
