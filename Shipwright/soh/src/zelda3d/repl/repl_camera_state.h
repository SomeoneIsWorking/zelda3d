#ifndef ZELDA3D_REPL_CAMERA_STATE_H
#define ZELDA3D_REPL_CAMERA_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

// Diagnostic camera controls are defined by core/zelda3d.c and applied by repl_runtime.cpp.
extern int gZelda3dCamOverride;
extern float gZelda3dCamEye[3];
extern float gZelda3dCamAt[3];

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_REPL_CAMERA_STATE_H
