// Zelda3D behavior: Boss_Fd (Volvagia's flying multipart form).
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_H

#include "../actor_behavior.h"

namespace Zelda3D {

struct BossFdRenderStatus {
    static constexpr int kModelCount = 8;
    int modelIds[kModelCount]{};
    long submitCounts[kModelCount]{};
    int drawAttempts = 0;
    int drawSuccesses = 0;
    int skinSegments = 0;
};

bool bossFdRenderStatus(Actor* actor, BossFdRenderStatus* outStatus);

class BossFdBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    void preUpdate(PlayState* play, Actor* actor) override;
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_H
