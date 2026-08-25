// Zelda3D camera-behavior framework — the structured home for ported OoT3D camera-mode functions.
//
// Camera_Update (SoH z_camera.c:7470, OoT3D FUN_002d84c4) dispatches to a per-mode function via
// sCameraFunctions[sCameraSettings[camera->setting].cameraModes[camera->mode].funcIdx]. Grezzo's 3DS
// version rewrote (or tuned) some of those mode functions — e.g. Camera_Normal0 became an 8-byte
// return-1 stub; Camera_Normal1 (OoT3D FUN_00239fd8, funcIdx CAM_FUNC_NORM1) is a full but subtly
// different implementation that drives the ~28-unit Δeye-Y at Kakariko even under matched Link pose.
//
// We port each divergent mode function as a CameraBehavior subclass, dispatched by funcIdx. A
// registered behavior FULLY OWNS that mode's camera math for the frame; when none is registered, the
// legacy SoH function runs. Migrate incrementally: today Normal1 is scaffolded but delegates to
// legacy; the actual port lands under `behaviors/camera/normal1.cpp`.
#ifndef ZELDA3D_BEHAVIORS_CAMERA_BEHAVIOR_H
#define ZELDA3D_BEHAVIORS_CAMERA_BEHAVIOR_H

#include "z64.h" // Camera, PlayState, s16

#ifdef __cplusplus
namespace Zelda3D {

// Base class for a ported OoT3D camera-mode function. Registered per funcIdx (CAM_FUNC_NORM1, ...);
// the seam in each SoH Camera_Normal*/Camera_Parallel*/... first checks the registry and delegates.
class CameraBehavior {
public:
    virtual ~CameraBehavior() = default;

    // The CAM_FUNC_* index this behavior owns.
    virtual s16 funcIdx() const = 0;

    // Run the OoT3D-faithful mode logic. Return true to indicate this behavior handled the frame;
    // false = fall through to the legacy SoH function. This lets a partial port land in stages:
    // return false during scaffolding, flip to true once the port is verified.
    virtual bool update(Camera* camera) = 0;
};

// Look up the behavior that owns `funcIdx`, or nullptr if none is registered. Defined in
// camera_behavior.cpp (the explicit registry — one entry per ported mode function).
CameraBehavior* findCameraBehavior(s16 funcIdx);

} // namespace Zelda3D
#endif

// C bridge for the z_camera.c seams — z_camera.c is compiled as C, so it cannot include the C++
// header directly. Returns 1 if a Zelda3D behavior handled the frame (skip legacy body), 0 to
// continue with the legacy body. `funcIdx` is the CAM_FUNC_* the caller identifies with.
#ifdef __cplusplus
extern "C" {
#endif
int Zelda3D_TryCameraBehavior(s16 funcIdx, Camera* camera);
#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_CAMERA_BEHAVIOR_H
