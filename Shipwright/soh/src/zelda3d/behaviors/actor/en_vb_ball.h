#ifndef ZELDA3D_BEHAVIORS_ACTOR_EN_VB_BALL_H
#define ZELDA3D_BEHAVIORS_ACTOR_EN_VB_BALL_H

#include "../actor_behavior.h"

namespace Zelda3D {

class EnVbBallBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_EN_VB_BALL_H
