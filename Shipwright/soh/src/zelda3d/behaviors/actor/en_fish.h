// Zelda3D behavior: En_Fish (ambient swimming fish) — animated model REPLACEMENT. OoT3D renders the
// fish from the keep zar's fishsmall CMB + fs2_swim CSAB (zelda_keep.zar fish/model/fishsmall.cmb +
// fish/Anim/fs2_swim.csab), mirroring N64's gFishSkel/gFishInWaterAnim from OBJECT_GAMEPLAY_KEEP.
// See en_fish.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_EN_FISH_H
#define ZELDA3D_BEHAVIORS_ACTOR_EN_FISH_H

#include "../actor_behavior.h"

namespace Zelda3D {

class EnFishBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Draws the OoT3D fish CMB animated by fs2_swim.csab at the actor's world.pos + shape.rot,
    // suppressing the N64 fish.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_EN_FISH_H
