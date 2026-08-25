// Zelda3D behavior: Bg_Bombwall (bombable cracked wall) — model REPLACEMENT. OoT3D draws it from the
// field-keep zar's c_bombwall CMBs (zelda_field_keep.zar Model/c_bombwall{before,after}_model.cmb),
// mirroring N64's gBgBombwall{Normal,Broken}DL from OBJECT_GAMEPLAY_FIELD_KEEP. See bombwall.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOMBWALL_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOMBWALL_H

#include "../actor_behavior.h"

namespace Zelda3D {

class BgBombwallBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Draws the OoT3D bombable-wall CMB (intact or broken, picked from the actor's live dList) at
    // world.pos + shape.rot, suppressing the N64 wall.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOMBWALL_H
