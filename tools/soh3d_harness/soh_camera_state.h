#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_CAMERA_STATE_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_CAMERA_STATE_H

extern "C" int SohState_Camera(float* eyeX, float* eyeY, float* eyeZ, float* atX, float* atY, float* atZ, float* upX,
                               float* upY, float* upZ, float* fov, short* roll, int* activeCamId);
extern "C" int SohState_SetCameraOverride(int enabled, const float* eye, const float* at, float fov);

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_CAMERA_STATE_H
