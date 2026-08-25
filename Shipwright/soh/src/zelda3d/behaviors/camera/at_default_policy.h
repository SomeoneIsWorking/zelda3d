#ifndef ZELDA3D_BEHAVIORS_CAMERA_AT_DEFAULT_POLICY_H
#define ZELDA3D_BEHAVIORS_CAMERA_AT_DEFAULT_POLICY_H

namespace Zelda3D {

// True when OoT3D's extra-Y producer, rather than the stock slope accumulator, owns unk_6C4.
bool CameraAtDefaultUsesExtraYBranch(int floorType, bool isGetItemAction);

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_CAMERA_AT_DEFAULT_POLICY_H
