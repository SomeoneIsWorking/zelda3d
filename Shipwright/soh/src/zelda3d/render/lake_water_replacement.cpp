#include "lake_water_replacement.h"

#include "../behaviors/actor/actor_assets.h"
#include "../diagnostics/model_tuning_query.h"
#include "actor_model_submission.h"
#include "model_queries.h"

int Zelda3D_TryDrawLakeWaterReplacement(PlayState* play, Actor* actor) {
    if (actor->id != ACTOR_BG_SPOT06_OBJECTS || actor->params != 2) {
        return -1;
    }
    const int modelId = Zelda3D_AutoModelId(ZSPOT06 "|c_s06beforewater");
    if (modelId < 0) {
        return 0;
    }
    // This two-bone CMB is a static surface in its bind pose. The generic route rejects it as
    // skinned, leaving only the additive caustic layer, so the actor-specific owner submits it.
    Zelda3D_DrawModelGL(play, modelId, actor,
                        Zelda3D_ModelScaleOrDefault(11, ZELDA3D_SPOT06_WATER_WORLD_SCALE), nullptr, 0.0f, nullptr,
                        nullptr);
    return 1;
}
