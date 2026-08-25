#include "title_rider_state.h"

#include "title_activity.h"
#include "../../cutscene/zelda3d_cutscene.h"

#include <cstdint>

namespace Zelda3D {

TitleRiderState& TitleRiderState::Instance() {
    static TitleRiderState instance;
    return instance;
}

void TitleRiderState::update(PlayState* play) {
    bool cueDiscontinuity = false;
    if (Zelda3D_TitleCsDidAdvance()) {
        mRider.step(play, Zelda3D_TitleCsFrame(), &cueDiscontinuity);
    }
}

void TitleRiderState::apply(PlayState* play, Actor* actor) {
    if (!TitleActivity::Instance().isActive() || actor == nullptr ||
        (actor->id != ACTOR_PLAYER && actor->id != ACTOR_EN_HORSE)) {
        return;
    }
    mRider.applyToActor(play, actor);
}

void TitleRiderState::release(PlayState* play) {
    mRider.releaseMount(play);
}

void TitleRiderState::resetRunState() {
    mRider.forgetActorsForNewRun();
}

const TitleRider& TitleRiderState::rider() const {
    return mRider;
}

} // namespace Zelda3D

extern "C" void Zelda3D_Title_RiderApply(PlayState* play, Actor* actor) {
    Zelda3D::TitleRiderState::Instance().apply(play, actor);
}

extern "C" int Zelda3D_Title_RiderState(float* outPos, int* outComputedYaw, int* outHorseWorldYaw,
                                        int* outHorseShapeYaw) {
    if (!Zelda3D_Title_IsActive()) {
        return 0;
    }

    const Zelda3D::TitleRider& rider = Zelda3D::TitleRiderState::Instance().rider();
    const float* computedPos = rider.pos();
    const Actor* horse = rider.horseActor();
    const float* position = computedPos;
    float renderedPos[3];
    if (horse != nullptr) {
        renderedPos[0] = horse->world.pos.x;
        renderedPos[1] = horse->world.pos.y;
        renderedPos[2] = horse->world.pos.z;
        position = renderedPos;
    }

    if (outPos != nullptr) {
        for (int i = 0; i < 3; ++i) {
            outPos[i] = position[i];
        }
    }
    if (outComputedYaw != nullptr) {
        *outComputedYaw = static_cast<int>(static_cast<int16_t>(rider.yaw()));
    }
    if (outHorseWorldYaw != nullptr) {
        *outHorseWorldYaw = horse != nullptr ? static_cast<int>(static_cast<int16_t>(horse->world.rot.y)) : 0x7FFFFFFF;
    }
    if (outHorseShapeYaw != nullptr) {
        *outHorseShapeYaw = horse != nullptr ? static_cast<int>(static_cast<int16_t>(horse->shape.rot.y)) : 0x7FFFFFFF;
    }
    return horse != nullptr ? 1 : 2;
}
