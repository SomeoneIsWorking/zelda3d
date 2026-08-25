#include "actor_bone_diagnostics.h"

#include "../../anim/pose_tracking.h"
#include "../../anim/skeleton_draw_bridge.h"
#include "../zelda3d_repl.h"

#include <stdio.h>
#include <string.h>

bool Zelda3D_ActorBoneDiagnosticsReplCommand(const char* command, const char* line, const char* outPath) {
    if (strcmp(command, "bonestats") != 0) {
        return false;
    }

    int modelId = gZelda3dLastAutoModel;
    (void)sscanf(line, "%*s %d", &modelId);
    Zelda3D_DumpBoneStats(modelId);
    Zelda3D_ReplReply(outPath, "bonestats model=%d -> run.log", modelId);
    return true;
}
