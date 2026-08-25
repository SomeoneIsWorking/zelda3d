#include "actor_special_replacements.h"

#include "../behaviors/actor_behavior_bridge.h"
#include "field_prop_replacements.h"
#include "kakariko_structure_replacements.h"
#include "lake_water_replacement.h"
#include "replacement_control.h"

int Zelda3D_TryDrawSpecialReplacement(PlayState* play, Actor* actor) {
    if (Zelda3D_AutoMode() == 2) {
        return -1;
    }

    int result = Zelda3D_TryDrawFieldPropReplacement(play, actor);
    if (result >= 0) {
        return result;
    }
    result = Zelda3D_TryDrawKakarikoStructureReplacement(play, actor);
    if (result >= 0) {
        return result;
    }
    result = Zelda3D_TryDrawLakeWaterReplacement(play, actor);
    if (result >= 0) {
        return result;
    }
    if (Zelda3D_TryActorModelDraw(play, actor)) {
        return 1;
    }
    if (Zelda3D_TryActorDeferredDraw(play, actor)) {
        return 0;
    }
    return -1;
}
