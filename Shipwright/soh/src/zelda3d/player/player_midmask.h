// Link equipment and hand-pose mesh visibility policy.
#ifndef ZELDA3D_PLAYER_MIDMASK_H
#define ZELDA3D_PLAYER_MIDMASK_H

#include "global.h"

namespace Zelda3D {

class LinkMidMask {
  public:
    unsigned long long compute(Player* player) const;

    unsigned long long overrideMask = ~0ull;
    bool overrideSet = false;

  private:
    unsigned long long boyMidMask(Player* player) const;
};

} // namespace Zelda3D

#endif // ZELDA3D_PLAYER_MIDMASK_H
