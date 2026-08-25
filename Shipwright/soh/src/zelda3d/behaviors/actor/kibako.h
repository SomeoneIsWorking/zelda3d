// Zelda3D behavior: Obj_Kibako (small wooden crate) — model REPLACEMENT. OoT3D draws the crate from the
// dungeon-keep zar's kibako CMB (zelda_dangeon_keep.zar Model/kibako_model.cmb), mirroring N64's
// gSmallWoodenBoxDL from OBJECT_GAMEPLAY_DANGEON_KEEP. See kibako.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_KIBAKO_H
#define ZELDA3D_BEHAVIORS_ACTOR_KIBAKO_H

#include "../actor_behavior.h"

namespace Zelda3D {

class ObjKibakoBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Draws the OoT3D crate CMB at the actor's world.pos + shape.rot, suppressing the N64 crate.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_KIBAKO_H
