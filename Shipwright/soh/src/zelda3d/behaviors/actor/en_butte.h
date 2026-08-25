// Zelda3D behavior: En_Butte (ambient butterfly) — animated model REPLACEMENT. OoT3D renders the
// butterfly from the field-keep zar's butterfly CMB + butterfly_fly CSAB (zelda_field_keep.zar
// Model/butterfly.cmb + Anim/butterfly_fly.csab), mirroring N64's gButterflySkel/gButterflyAnim
// from OBJECT_GAMEPLAY_FIELD_KEEP. See en_butte.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_EN_BUTTE_H
#define ZELDA3D_BEHAVIORS_ACTOR_EN_BUTTE_H

#include "../actor_behavior.h"

namespace Zelda3D {

class EnButteBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Draws the OoT3D butterfly CMB animated by butterfly_fly.csab at the actor's world.pos +
    // shape.rot, suppressing the N64 butterfly. Falls through to N64 during the fairy-transform flash.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_EN_BUTTE_H
