// Zelda3D behavior: En_Dog — per-material CONSTANT-colour override so the dog renders
// coloured instead of solid black. See en_dog.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_EN_DOG_H
#define ZELDA3D_BEHAVIORS_ACTOR_EN_DOG_H

#include "../actor_behavior.h"

namespace Zelda3D {

class EnDogBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    void applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_EN_DOG_H
