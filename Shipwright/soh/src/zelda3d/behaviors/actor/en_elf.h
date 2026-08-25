// Zelda3D behavior: En_Elf (Navi + generic fairies) — model REPLACEMENT via native billboards.
// OoT3D's Navi is a two-sprite additive-glow effect, NOT a CMB-skeleton actor. See
// en_elf.cpp + oot3d-decomp/docs/en_elf_navi.md.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_EN_ELF_H
#define ZELDA3D_BEHAVIORS_ACTOR_EN_ELF_H

#include "../actor_behavior.h"

namespace Zelda3D {

class EnElfBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Emits an outer glow + inner core billboard at actor.world.pos, coloured from the
    // live EnElf outer/inner colors and sized by the standard fairy scale curve.
    // Suppresses the N64 sprite. Falls through only when the actor is invisibly gated
    // (fairyFlags & 8, or the "vanished" state 8).
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_EN_ELF_H
