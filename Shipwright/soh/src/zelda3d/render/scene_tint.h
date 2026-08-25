// Flat scene-light tint applied to converted unlit OoT3D model content.
#ifndef ZELDA3D_RENDER_SCENE_TINT_H
#define ZELDA3D_RENDER_SCENE_TINT_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_SceneTint(PlayState* play, u8 output[3]);
extern float gZelda3dTintDiff;
extern float gZelda3dTintMul;

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_SCENE_TINT_H
