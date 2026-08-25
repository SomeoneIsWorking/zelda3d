// Zelda3D behavior: En_Door (standard hinged house/dungeon door). Model-REPLACEMENT behavior — OoT3D
// draws standard doors from the KEEP zar's door CMB (zelda_keep.zar door/model/m_Fnormaldoor), not a
// standalone actor object. See door.cpp and oot3d-decomp/docs/en_door.md.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_DOOR_H
#define ZELDA3D_BEHAVIORS_ACTOR_DOOR_H

#include "../actor_behavior.h"

namespace Zelda3D {

class EnDoorBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Draws the OoT3D door CMB at the actor's world.pos + shape.rot.y, suppressing the N64 door.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

extern "C" {
extern int gZelda3dDoorBone;
extern int gZelda3dDoorAxis;
extern float gZelda3dDoorGain;
extern int gZelda3dDoorHold;
}

#endif // ZELDA3D_BEHAVIORS_ACTOR_DOOR_H
