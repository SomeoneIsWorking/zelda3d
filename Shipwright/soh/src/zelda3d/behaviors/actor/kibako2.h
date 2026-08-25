// Zelda3D behavior: Obj_Kibako2 (large wooden crate) — model REPLACEMENT. OoT3D draws the large crate
// from its own object zar (zelda_kibako2.zar model/CIkibako_model.cmb), mirroring N64's gLargeCrateDL
// from OBJECT_KIBAKO2. See kibako2.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_KIBAKO2_H
#define ZELDA3D_BEHAVIORS_ACTOR_KIBAKO2_H

#include "../actor_behavior.h"

namespace Zelda3D {

class ObjKibako2Behavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Draws the OoT3D large-crate CMB at the actor's world.pos + shape.rot, suppressing the N64 crate.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_KIBAKO2_H
