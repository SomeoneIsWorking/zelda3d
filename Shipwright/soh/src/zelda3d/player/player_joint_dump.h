// Live player joint-table CSV capture used by retarget diagnostics.
#ifndef ZELDA3D_PLAYER_JOINT_DUMP_H
#define ZELDA3D_PLAYER_JOINT_DUMP_H

#include "global.h"

#include <cstdio>

namespace Zelda3D {

class LinkJointDump {
  public:
    ~LinkJointDump();

    bool start(const char* path, int frameCount);
    void capture(const Player* player);

  private:
    void stop();

    FILE* mFile = nullptr;
    int mRemaining = 0;
    int mCaptureIndex = 0;
};

} // namespace Zelda3D

#endif // ZELDA3D_PLAYER_JOINT_DUMP_H
