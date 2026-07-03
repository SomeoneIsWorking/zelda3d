// Zelda3D camera-behavior registry + C bridge. See camera_behavior.h.
#include "z64.h"
#include "camera_behavior.h"
#include "camera/normal1.h"

namespace Zelda3D {

CameraBehavior* findCameraBehavior(s16 funcIdx) {
    static Normal1Behavior sNormal1;
    switch (funcIdx) {
        case CAM_FUNC_NORM1: return &sNormal1;
        default:             return nullptr;
    }
}

} // namespace Zelda3D

extern "C" int Zelda3D_TryCameraBehavior(s16 funcIdx, Camera* camera) {
    if (Zelda3D::CameraBehavior* b = Zelda3D::findCameraBehavior(funcIdx)) {
        return b->update(camera) ? 1 : 0;
    }
    return 0;
}
