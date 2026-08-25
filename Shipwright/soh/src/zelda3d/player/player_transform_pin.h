// Deterministic Link transform pin used by retarget diagnostics.
#ifndef ZELDA3D_PLAYER_TRANSFORM_PIN_H
#define ZELDA3D_PLAYER_TRANSFORM_PIN_H

#include "global.h"

namespace Zelda3D {

class LinkTransformPin {
  public:
    void set(const Player* player, bool enabled);
    void apply(PlayState* play, Actor* actor) const;
    bool enabled() const;

  private:
    bool mEnabled = false;
    Vec3f mPosition{};
    s16 mYaw = 0;
};

} // namespace Zelda3D

#endif // ZELDA3D_PLAYER_TRANSFORM_PIN_H
