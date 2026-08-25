#include "scene_draw.h"

#include "../core/zelda3d_runtime.h"

#include "../tables/zelda3d_scene_names.inc"

const char* Zelda3D_SceneName(PlayState* play) {
    s32 sceneNumber = play->sceneNum;
    if (sceneNumber < 0 || sceneNumber >= (s32)ARRAY_COUNT(kZelda3dSceneNames)) {
        return NULL;
    }
    return kZelda3dSceneNames[sceneNumber];
}

int Zelda3D_ShouldSuppressBgImageSkybox(PlayState* play) {
    return (play != NULL && Zelda3D_Enabled()) ? 1 : 0;
}
