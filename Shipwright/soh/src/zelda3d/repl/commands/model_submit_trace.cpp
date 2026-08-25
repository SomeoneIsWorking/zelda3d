#include "model_submit_trace.h"

#include "../../player/player_pose_scan.h"
#include "../zelda3d_repl.h"
#include <fast/zelda3d_instrumentation.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool Zelda3D_ModelSubmitTraceReplCommand(const char* command, const char* line, const char* outPath) {
    if (strcmp(command, "mptrace") != 0) {
        return false;
    }

    // Trace every submit of one model with interpolation step and world/clip anchor.
    char target[16] = { 0 };
    sscanf(line, "%*s %15s", target);
    if (strcmp(target, "link") == 0) {
        gZelda3dTraceModelId = Zelda3D_LinkModelId();
    } else {
        gZelda3dTraceModelId = atoi(target);
    }
    Zelda3D_ReplReply(outPath, "mptrace modelId=%d", gZelda3dTraceModelId);
    return true;
}
