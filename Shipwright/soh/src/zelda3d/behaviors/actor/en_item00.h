// Zelda3D behavior: En_Item00 (dropped/placed collectible) — model REPLACEMENT for its RUPEE types.
// The dropped green/blue/red/gold/purple rupees draw the N64 gRupeeDL with a per-color texture swap,
// exactly like En_Ex_Ruppy; we render the OoT3D rupee CMB masked to the matching color mesh and fall
// through to the N64 draw for the non-rupee item types (hearts, magic, seeds, …). See en_item00.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_EN_ITEM00_H
#define ZELDA3D_BEHAVIORS_ACTOR_EN_ITEM00_H

#include "../actor_behavior.h"

namespace Zelda3D {

class EnItem00Behavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Draws the OoT3D rupee CMB for rupee-type drops (masked to the color mesh); returns false for
    // every other item type so the N64 draw handles it.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_EN_ITEM00_H
