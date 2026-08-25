// Zelda3D behavior: Boss_Fd (Volvagia's flying multipart form).
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_H

#include "../actor_behavior.h"

namespace Zelda3D {

class BossFdBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    void preUpdate(PlayState* play, Actor* actor) override;
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_H
