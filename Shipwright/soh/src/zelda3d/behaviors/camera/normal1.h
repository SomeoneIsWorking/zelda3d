// Zelda3D behavior: Camera_Normal1 — ported from OoT3D FUN_00239fd8.
//
// SoH z_camera.c:1538 Camera_Normal1 and OoT3D FUN_00239fd8 both drive the CAM_FUNC_NORM1 mode
// (spring-follow camera with pitch clamp + yaw drift). SoH's own faithful N64 Camera_Normal1 runs
// and is AT PARITY with OoT3D for the modes exercised at Kakariko — this module is a no-op delegate
// (`update` returns false → legacy runs). No body port is needed.
//
// The "~28-unit Kakariko eye-Y drift" that once motivated a port was a TEST-HARNESS LinkAge artifact,
// not a camera-code divergence: the oracle loaded a CHILD-Link savestate (Player_GetHeight=44) while
// SoH booted its ADULT default (=68); 68-44=24 = the observed |Δat|, propagated through the IDENTICAL
// Camera_CalcAtDefault→Camera_Normal1 flow. With ages matched (soh_setage), |Δeye| drops 27.96→2.07,
// |Δat| 24.10→0.10 (empirically confirmed 2026-07-03). See oot3d-decomp/docs/gameplay_firstdiv.md
// :1243-1323 and docs/re-frontier.md camera.normal1. The separate Grezzo Δ-A extra-Y block
// (Camera_CalcAtDefault, gated on player-state bit 0x100, inert at Kakariko-idle) is tracked as
// re-frontier item camera.calc-at-default-ybias.
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
