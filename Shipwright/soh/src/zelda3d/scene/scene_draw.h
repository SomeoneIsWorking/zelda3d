// Engine-facing scene-room replacement choke points.
#ifndef ZELDA3D_SCENE_DRAW_H
#define ZELDA3D_SCENE_DRAW_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_ShouldSuppressBgImageSkybox(PlayState* play);
const char* Zelda3D_SceneName(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_SCENE_DRAW_H
