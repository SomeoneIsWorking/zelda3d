// Generic object-bank-to-OoT3D-model automatic replacement engine.
#ifndef ZELDA3D_RENDER_ACTOR_AUTO_REPLACEMENT_H
#define ZELDA3D_RENDER_ACTOR_AUTO_REPLACEMENT_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_TryAuto(PlayState* play, Actor* actor);
void Zelda3D_AutoRetryOnSceneChange(PlayState* play);
void Zelda3D_RecordAutoCalibration(int objectId, float height, float footprintX, float footprintZ);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_ACTOR_AUTO_REPLACEMENT_H
