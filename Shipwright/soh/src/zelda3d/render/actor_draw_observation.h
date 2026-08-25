// Last submitted actor-model placement used by framing and draw diagnostics.
#ifndef ZELDA3D_RENDER_ACTOR_DRAW_OBSERVATION_H
#define ZELDA3D_RENDER_ACTOR_DRAW_OBSERVATION_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

extern s32 sZelda3dSelDrawModel;
extern float sZelda3dSelDrawScale;
extern float sZelda3dSelDrawGroundOff;
extern s32 sZelda3dSelDrawDsHave;
extern float sZelda3dSelDrawDsLiftY;
extern float sZelda3dSelDrawDsLocal[3];
extern float gZelda3dAimCenter[3];
extern float gZelda3dAimRadius;

void Zelda3D_RecordActorDrawSubmission(Actor* actor, int modelId, float worldScale, float groundOffset,
                                       int hasDrawSpaceTransform, float drawSpaceLiftY, const float drawSpaceLocal[3]);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_ACTOR_DRAW_OBSERVATION_H
