// SoH3D behavior: Obj_Kibako2 (large wooden crate) — model REPLACEMENT. OoT3D draws the large crate
// from its own object zar (zelda_kibako2.zar model/CIkibako_model.cmb), mirroring N64's gLargeCrateDL
// from OBJECT_KIBAKO2. See kibako2.cpp.
#ifndef SOH3D_BEHAVIORS_ACTOR_KIBAKO2_H
#define SOH3D_BEHAVIORS_ACTOR_KIBAKO2_H

#include "../actor_behavior.h"

namespace SoH3D {

class ObjKibako2Behavior : public ActorBehavior {
public:
    s16 actorId() const override;
    // Draws the OoT3D large-crate CMB at the actor's world.pos + shape.rot, suppressing the N64 crate.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace SoH3D

#endif // SOH3D_BEHAVIORS_ACTOR_KIBAKO2_H
