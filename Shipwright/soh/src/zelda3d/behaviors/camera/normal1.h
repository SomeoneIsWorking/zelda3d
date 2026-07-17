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
//
// NOTE (2026-07-17): the 28-unit drift is NOT the extra Camera_CalcAtDefault at.y term first
// suspected — that term (`at.y += player[0x1760]·-0.01`) is a Grezzo motion-only Y-bias that decays
// to 0 at rest, so it is exactly 0 at the matched/idle pose this drift was measured under. Cause is
// unlocalized; likely the eye distance/pitch path. Next step is an empirical converged-pose eye.y
// A/B (SoH `posinfo` vs oracle `cam_eye`), not more static diffing. See docs/re-frontier.md
// camera.normal1 + debug_journal/2026-07-17-oot3d-dat-constants-and-camera-normal1-yoffset.md.
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
