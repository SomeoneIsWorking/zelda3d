// Per-frame scene-light selection and submission to the OoT3D renderer.
#ifndef ZELDA3D_RENDER_SCENE_LIGHTING_SUBMISSION_H
#define ZELDA3D_RENDER_SCENE_LIGHTING_SUBMISSION_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_UpdateSceneLighting(PlayState* play);

extern int gZelda3dLightDirOverride;
extern float gZelda3dLightDirLast[3];

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_SCENE_LIGHTING_SUBMISSION_H
