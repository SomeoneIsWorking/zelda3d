// Player skeleton correction-table ownership and diagnostic Euler conversion.
#ifndef ZELDA3D_PLAYER_RETARGET_H
#define ZELDA3D_PLAYER_RETARGET_H

#include "global.h"
#include "zelda3d_link.h"

void Zelda3D_EulerToMat3(float cxDeg, float cyDeg, float czDeg, float* out);
void Zelda3D_Mat3ToEuler(const float* matrix, float* outDeg);

namespace Zelda3D {

class LinkRetarget {
  public:
    void ensure();
    void reset();

    Zelda3dBoneCorr table[25];
    bool inited = false;
    Vec3s frozenJoints[40];
    int frozenCount = 0;
    int freezeReq = 0;
};

} // namespace Zelda3D

#endif // ZELDA3D_PLAYER_RETARGET_H
