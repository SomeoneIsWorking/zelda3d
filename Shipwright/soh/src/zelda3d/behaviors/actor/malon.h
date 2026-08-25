// Zelda3D behaviors: Malon — En_Ma1 (child) and En_Ma2/En_Ma3 (adult). Head/torso track + eye/mouth
// material-anim. See malon.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_MALON_H
#define ZELDA3D_BEHAVIORS_ACTOR_MALON_H

#include "../actor_behavior.h"

namespace Zelda3D {

// En_Ma1 child Malon: head bone 7, torso 6, eye material 3, mouth material 4.
class ChildMalonBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    void applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) override;
};

// En_Ma2 (Lon Lon) / En_Ma3 (post-credits) adult Malon — same OoT3D rig: head bone 8, torso 7, eye
// material 4, mouth material 5. Registered for both ACTOR_EN_MA2 and ACTOR_EN_MA3.
class AdultMalonBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    void applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_MALON_H
