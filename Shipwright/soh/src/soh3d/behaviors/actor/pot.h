// SoH3D behavior: En_Tubo_Trap (flying-trap pot) — model REPLACEMENT. OoT3D draws the dungeon pot
// from the dungeon-keep zar's tubo CMB (zelda_dangeon_keep.zar Model/tubo_model.cmb), mirroring N64's
// gPotDL from OBJECT_GAMEPLAY_DANGEON_KEEP. See pot.cpp.
#ifndef SOH3D_BEHAVIORS_ACTOR_POT_H
#define SOH3D_BEHAVIORS_ACTOR_POT_H

#include "../actor_behavior.h"

namespace SoH3D {

class EnTuboTrapBehavior : public ActorBehavior {
public:
    s16 actorId() const override;
    // Draws the OoT3D pot CMB at the actor's world.pos + shape.rot, suppressing the N64 pot.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace SoH3D

#endif // SOH3D_BEHAVIORS_ACTOR_POT_H
