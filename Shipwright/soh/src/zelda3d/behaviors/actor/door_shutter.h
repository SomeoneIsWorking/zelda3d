// Zelda3D behavior: Door_Shutter (dungeon shutter / boss doors) — model REPLACEMENT. OoT3D draws
// per-dungeon shutter/boss doors from the dungeon's object zar (e.g. zelda_hidan_objects.zar
// m_Fshutter1_model.cmb for Fire), mirroring N64's per-object sShutterInfo table in z_door_shutter.c.
// See door_shutter.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_DOOR_SHUTTER_H
#define ZELDA3D_BEHAVIORS_ACTOR_DOOR_SHUTTER_H

#include "../actor_behavior.h"

namespace Zelda3D {

class DoorShutterBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Draws the per-scene OoT3D shutter/boss door CMB at the actor's world.pos + shape.rot.y,
    // suppressing the N64 door. Returns false when the scene has no mapped CMB (fall through).
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_DOOR_SHUTTER_H
