// Zelda3D behavior: Obj_Hamishi (silver rock — Megaton-Hammer boulder) — model REPLACEMENT. OoT3D
// draws it from the field-keep zar's isi01 CMB (zelda_field_keep.zar Model/obj_isi01_model.cmb),
// mirroring N64's gSilverRockDL from OBJECT_GAMEPLAY_FIELD_KEEP. See hamishi.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_HAMISHI_H
#define ZELDA3D_BEHAVIORS_ACTOR_HAMISHI_H

#include "../actor_behavior.h"

namespace Zelda3D {

class ObjHamishiBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Draws the OoT3D silver-rock CMB at the actor's world.pos + shape.rot, suppressing the N64 rock.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_HAMISHI_H
