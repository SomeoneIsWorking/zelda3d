#include "archive_diagnostics.h"
#include "../../behaviors/actor/actor_assets.h"

#include "../../diagnostics/zelda3d_diagnostics.h"
#include "../../diagnostics/model_tuning_query.h"
#include "../../render/model_queries.h"
#include "../zelda3d_repl.h"

bool Zelda3D_ArchiveDiagnosticsReplCommand(PlayState* play, const char* command, const char* line,
                                           const char* outPath) {
    if (strcmp(command, "archinfo") == 0) {
        // #77 diagnostic: dump the well-arch (Idohashira) CMB geometry anchoring vs the actor.
        // minY/height are LOCAL CMB units; multiply by worldScale for world units. Predicts where
        // the model's bottom/top land relative to the selected actor's world Y.
        int mid = Zelda3D_AutoModelId(ZSPOT01 "|c_s01idohashira");
        float miny = Zelda3D_AutoModelMinY(mid);
        float h = Zelda3D_AutoModelHeight(mid);
        float ex = 0.0f, ez = 0.0f;
        Zelda3D_AutoModelExtentXZ(mid, &ex, &ez);
        float ws = Zelda3D_ModelScaleOrDefault(8, ZELDA3D_SPOT01_WORLD_SCALE);
        float ay = (gZelda3dSelActor != NULL) ? gZelda3dSelActor->world.pos.y : 0.0f;
        Zelda3D_ReplReply(outPath,
                          "archinfo mid=%d localMinY=%.1f localH=%.1f extXZ=(%.1f,%.1f) wscale=%.5f "
                          "| world: bottom=Y%+.1f top=Y%+.1f (actorY=%.1f) -> drawnBottom=%.1f drawnTop=%.1f",
                          mid, miny, h, ex, ez, ws, miny * ws, (miny + h) * ws, ay, ay + miny * ws,
                          ay + (miny + h) * ws);
    } else {
        return false;
    }
    return true;
}
