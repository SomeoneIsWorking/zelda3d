// Zelda3D behavior: Camera_Normal1 — see normal1.h.
//
// Current status: SCAFFOLDING ONLY. `update()` returns false → SoH's legacy Camera_Normal1
// (z_camera.c:1538) runs and the harness reports the ~28-unit Δeye-Y at Kakariko as before. Once
// the port is landed and A/B'd against oot3d-decomp/build/decomp/00239fd8.c (OoT3D FUN_00239fd8),
// flip the return to true.
#include "z64.h"
#include "normal1.h"

namespace Zelda3D {

s16 Normal1Behavior::funcIdx() const {
    return CAM_FUNC_NORM1;
}

bool Normal1Behavior::update(Camera* camera) {
    // TODO(port-camera-normal1): port OoT3D FUN_00239fd8 body here. Start by reading yOffset/dist/
    // pitch defaults from sCameraSettings via the same CAM_DATA_* record SoH uses; the height-scale
    // formula is identical (playerHeight * PCT × (1 + CAM_YOFFSET_NORM_PCT - CAM_YOFFSET_NORM_PCT *
    // 68/playerHeight)). Divergence source is likely in the swing/pitch-clamp block (SoH
    // z_camera.c:1699-1719) or Camera_CalcAtDefault yOffset ceiling.
    return false;
}

} // namespace Zelda3D
