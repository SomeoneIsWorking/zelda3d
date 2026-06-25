// SoH3D behavior: Obj_Comb (field beehive) — model REPLACEMENT. OoT3D draws the beehive from the
// field-keep zar's hatisu CMB (zelda_field_keep.zar Model/hatisu_model.cmb), mirroring N64's
// gFieldBeehiveDL from OBJECT_GAMEPLAY_FIELD_KEEP. See comb.cpp.
#ifndef SOH3D_BEHAVIORS_ACTOR_COMB_H
#define SOH3D_BEHAVIORS_ACTOR_COMB_H

#include "../actor_behavior.h"

namespace SoH3D {

class ObjCombBehavior : public ActorBehavior {
public:
    s16 actorId() const override;
    // Draws the OoT3D beehive CMB at the actor's world.pos + shape.rot, suppressing the N64 beehive.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace SoH3D

#endif // SOH3D_BEHAVIORS_ACTOR_COMB_H
