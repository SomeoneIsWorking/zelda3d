// Zelda3D behavior: En_Mu (Market Day haggling townspeople) — per-material CONSTANT-colour
// overrides so the marketpeople clothing renders coloured instead of solid black. See en_mu.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_EN_MU_H
#define ZELDA3D_BEHAVIORS_ACTOR_EN_MU_H

#include "../actor_behavior.h"

namespace Zelda3D {

class EnMuBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    void applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_EN_MU_H
