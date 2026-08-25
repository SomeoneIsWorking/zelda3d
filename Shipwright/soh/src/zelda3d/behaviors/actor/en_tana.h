// Zelda3D behavior: En_Tana (shop shelves) — model REPLACEMENT. OoT3D draws the wooden shop shelf from
// zelda_shop_tana.zar Model/shop_tana01_model.cmb, mirroring N64's gShopDungenWoodenShelvesDL from
// OBJECT_SHOP_DUNGEN. Stone variants (params 1/2) have no OoT3D actor CMB -> N64 fallback. See en_tana.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_EN_TANA_H
#define ZELDA3D_BEHAVIORS_ACTOR_EN_TANA_H

#include "../actor_behavior.h"

namespace Zelda3D {

class EnTanaBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Draws the OoT3D wooden shop shelf at world.pos + shape.rot for params 0, suppressing the N64
    // shelf; falls through for the stone variants.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_EN_TANA_H
