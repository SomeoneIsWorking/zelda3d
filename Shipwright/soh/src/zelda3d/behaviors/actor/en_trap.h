// Zelda3D behavior: En_Trap (sliding blade / floor trap) — model REPLACEMENT. OoT3D draws the same
// sliding blade from zelda_yukabyun.zar Model/trap_yukabyun_model.cmb (a single static mesh).
// See en_trap.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_EN_TRAP_H
#define ZELDA3D_BEHAVIORS_ACTOR_EN_TRAP_H

#include "../actor_behavior.h"

namespace Zelda3D {

class EnTrapBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_EN_TRAP_H
