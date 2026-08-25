// Deterministic selected-actor pickup driver used by Link RE diagnostics.
#ifndef ZELDA3D_PLAYER_GRAB_DRIVER_H
#define ZELDA3D_PLAYER_GRAB_DRIVER_H

#include "global.h"

namespace Zelda3D {

class LinkGrabDriver {
  public:
    void start(int frames);
    void walkInject(PlayState* play);

  private:
    int mFrames = 0;
};

} // namespace Zelda3D

#endif // ZELDA3D_PLAYER_GRAB_DRIVER_H
