#include "soh_camera_state.h"

#include "global.h"
#include "z64camera.h"

extern "C" int SohState_Camera(float* eyeX, float* eyeY, float* eyeZ, float* atX, float* atY, float* atZ, float* upX,
                               float* upY, float* upZ, float* fov, short* roll, int* activeCamId) {
    if (gPlayState == nullptr || gPlayState->activeCamera < 0 || gPlayState->activeCamera >= NUM_CAMS) {
        return 0;
    }
    const Camera* camera = gPlayState->cameraPtrs[gPlayState->activeCamera];
    if (camera == nullptr) {
        return 0;
    }
    *eyeX = camera->eye.x;
    *eyeY = camera->eye.y;
    *eyeZ = camera->eye.z;
    *atX = camera->at.x;
    *atY = camera->at.y;
    *atZ = camera->at.z;
    *upX = camera->up.x;
    *upY = camera->up.y;
    *upZ = camera->up.z;
    *fov = camera->fov;
    *roll = camera->roll;
    *activeCamId = gPlayState->activeCamera;
    return 1;
}
