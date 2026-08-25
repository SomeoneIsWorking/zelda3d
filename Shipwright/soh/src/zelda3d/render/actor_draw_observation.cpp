#include "actor_draw_observation.h"
#include "../anim/pose_tracking.h"
#include "../diagnostics/actor_selection.h"

s32 sZelda3dSelDrawModel = -1;
float sZelda3dSelDrawScale = 1.0f;
float sZelda3dSelDrawGroundOff = 0.0f;
s32 sZelda3dSelDrawDsHave = 0;
float sZelda3dSelDrawDsLiftY = 0.0f;
float sZelda3dSelDrawDsLocal[3] = { 0.0f, 0.0f, 0.0f };
float gZelda3dAimCenter[3] = { 0.0f, 0.0f, 0.0f };
float gZelda3dAimRadius = 50.0f;

void Zelda3D_RecordActorDrawSubmission(Actor* actor, int modelId, float worldScale, float groundOffset,
                                       int hasDrawSpaceTransform, float drawSpaceLiftY, const float drawSpaceLocal[3]) {
    if (actor == NULL || actor != gZelda3dSelActor) {
        return;
    }

    sZelda3dSelDrawModel = modelId;
    sZelda3dSelDrawScale = worldScale;
    sZelda3dSelDrawGroundOff = groundOffset;
    sZelda3dSelDrawDsHave = hasDrawSpaceTransform;
    sZelda3dSelDrawDsLiftY = hasDrawSpaceTransform ? drawSpaceLiftY : 0.0f;
    sZelda3dSelDrawDsLocal[0] = hasDrawSpaceTransform ? drawSpaceLocal[0] : 0.0f;
    sZelda3dSelDrawDsLocal[1] = hasDrawSpaceTransform ? drawSpaceLocal[1] : 0.0f;
    sZelda3dSelDrawDsLocal[2] = hasDrawSpaceTransform ? drawSpaceLocal[2] : 0.0f;
    Zelda3D_SetTrackPosedMinY(modelId, 1);
}
