// Zelda3D behavior: Obj_Switch (floor / eye / crystal switches). Model-REPLACEMENT behavior — OoT3D
// draws switches from the DANGEON_KEEP zar's switch_N CMBs (zelda_dangeon_keep.zar switch_*_model.cmb),
// not a standalone actor object. See obj_switch.cpp and oot3d-decomp/docs/keep_objects.md.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_OBJ_SWITCH_H
#define ZELDA3D_BEHAVIORS_ACTOR_OBJ_SWITCH_H

#include "../actor_behavior.h"

namespace Zelda3D {

class ObjSwitchBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Draws the OoT3D switch CMB for the actor's type/subType (params). Returns false for
    // not-yet-ported subtypes so the N64 switch still draws.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_OBJ_SWITCH_H
