// Zelda3D behavior: Camera_Normal1 — ported from OoT3D FUN_00239fd8.
//
// SoH z_camera.c:1538 Camera_Normal1 (~230 lines) and OoT3D FUN_00239fd8 (~418 lines) both drive
// the CAM_FUNC_NORM1 mode (spring-follow camera with pitch clamp + yaw drift). Kakariko Village
// runs live under setting=CAM_SET_NORMAL1 mode=CAM_MODE_NORMAL (verified via harness az_cam probe,
// soh3d 28f24f23) and shows a persistent 28-unit eye-Y drift vs OoT3D even with matched Link pose.
//
// This module reimplements the OoT3D function faithfully; the seam lives in z_camera.c:1538 as an
// early Zelda3D_TryCameraBehavior(CAM_FUNC_NORM1, camera) check. During staged porting, `update`
// returns false → legacy runs; when the port is verified against the Kakariko sweep it flips to
// true and takes over.
#ifndef ZELDA3D_BEHAVIORS_CAMERA_NORMAL1_H
#define ZELDA3D_BEHAVIORS_CAMERA_NORMAL1_H

#include "../camera_behavior.h"

namespace Zelda3D {

class Normal1Behavior : public CameraBehavior {
public:
    s16 funcIdx() const override;
    bool update(Camera* camera) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_CAMERA_NORMAL1_H
