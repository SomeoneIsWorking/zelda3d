// Zelda3D behavior: Obj_Oshihiki (push/pull block) — model REPLACEMENT. OoT3D draws a per-dungeon
// themed brick CMB (zelda_dangeon_keep.zar Model/brick_15_<theme>_<Size>_model.cmb) in place of N64's
// single env-tinted gPushBlockDL from OBJECT_GAMEPLAY_DANGEON_KEEP. See push_block.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_PUSH_BLOCK_H
#define ZELDA3D_BEHAVIORS_ACTOR_PUSH_BLOCK_H

#include "../actor_behavior.h"

namespace Zelda3D {

class ObjOshihikiBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Draws the OoT3D themed push-block brick at world.pos + shape.rot, scaled per the actor's size
    // param, suppressing the N64 block. Falls through for scenes with no OoT3D brick.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_PUSH_BLOCK_H
