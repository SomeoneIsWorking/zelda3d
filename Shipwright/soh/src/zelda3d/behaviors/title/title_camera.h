#ifndef ZELDA3D_BEHAVIORS_TITLE_TITLE_CAMERA_H
#define ZELDA3D_BEHAVIORS_TITLE_TITLE_CAMERA_H

#include "global.h"

#ifdef __cplusplus
namespace Zelda3D {

// Advance the title cutscene cursor, resolve its OP97 camera, and publish that camera to both
// PlayState view representations.
void UpdateTitleCamera(PlayState* play);

} // namespace Zelda3D

extern "C" {
#endif

extern const float kZelda3dTitleEye[3];
int Zelda3D_Title_CameraState(float* outEye, float* outAt, float* outUp, float* outFov);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_CAMERA_H
