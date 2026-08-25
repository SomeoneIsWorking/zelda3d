#include "actor_draw.h"

#include "../../core/zelda3d_runtime.h"
#include "../../render/replacement_calibration.h"
#include "../../render/replacement_catalog.h"
#include "../../render/replacement_control.h"

#include "../../tables/zelda3d_object_zars.inc"

int Zelda3D_ActorHasReplacement(PlayState* play, Actor* actor) {
    if (!Zelda3D_Enabled() || actor == NULL) {
        return 0;
    }
    if (Zelda3D_AutoMode() != 2) {
        if (actor->id == ACTOR_OBJ_HANA) {
            int variant = actor->params & 3;
            if (variant == 0 || variant == 1 || variant == 2) {
                return 1;
            }
        }
        if (actor->id == ACTOR_EN_ISHI) {
            return 1;
        }
        for (s32 index = 0; index < Zelda3D_ExplicitReplacementCount(); index++) {
            const Zelda3D_ModelEntry* entry = Zelda3D_ExplicitReplacementAt(index);
            if (entry != NULL && entry->actorId == actor->id) {
                return 1;
            }
        }
    }
    if (Zelda3D_AutoMode() >= 1) {
        int objectId = Zelda3D_ActorObjectId(play, actor);
        const Zelda3D_AutoEntry* calibration = Zelda3D_AutoCalibrationAt(objectId);
        if (objectId >= 0 && objectId < (int)ARRAY_COUNT(kZelda3dObjectZars) && kZelda3dObjectZars[objectId] != NULL &&
            objectId != OBJECT_KANBAN && calibration != NULL && calibration->state != 3) {
            return 1;
        }
    }
    return 0;
}
