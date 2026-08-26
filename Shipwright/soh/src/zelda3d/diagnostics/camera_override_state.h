// Diagnostic camera pose held across frames by the control surface.
#ifndef ZELDA3D_DIAGNOSTICS_CAMERA_OVERRIDE_STATE_H
#define ZELDA3D_DIAGNOSTICS_CAMERA_OVERRIDE_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

extern int gZelda3dCamOverride;
extern float gZelda3dCamEye[3];
extern float gZelda3dCamAt[3];
extern float gZelda3dCamFov;
extern int gZelda3dCamFovOverride;

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_DIAGNOSTICS_CAMERA_OVERRIDE_STATE_H
