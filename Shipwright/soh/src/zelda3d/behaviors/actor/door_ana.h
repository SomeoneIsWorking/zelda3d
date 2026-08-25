// Zelda3D behavior: Door_Ana (grotto hole / trapdoor entrance) — model REPLACEMENT. OoT3D draws the
// grotto hole from the field-keep zar's ana01 CMB (zelda_field_keep.zar Model/ana01_modelT.cmb,
// "ana" = hole), mirroring N64's gGrottoDL from OBJECT_GAMEPLAY_FIELD_KEEP. See door_ana.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_DOOR_ANA_H
#define ZELDA3D_BEHAVIORS_ACTOR_DOOR_ANA_H

#include "../actor_behavior.h"

namespace Zelda3D {

class DoorAnaBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Draws the OoT3D grotto-hole CMB at the actor's world.pos + shape.rot, suppressing the N64 hole.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_DOOR_ANA_H
