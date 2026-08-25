#include "render_debug_control.h"

#include "../../render/model_group_diagnostics.h"
#include "../../render/room_render.h"
#include "../../scene/scene_transform.h"
#include "../zelda3d_repl.h"
#include <fast/zelda3d_instrumentation.h>

#include <stdio.h>
#include <string.h>

bool Zelda3D_RenderDebugReplCommand(const char* command, const char* line, const char* outPath) {
    float value;
    int group;
    if (strcmp(command, "statecheck") == 0 && sscanf(line, "%*s %f", &value) == 1) {
        gZelda3dStateCheck = static_cast<int>(value);
        Zelda3D_ReplReply(outPath, "statecheck=%d (1=log any GL state our render pass fails to restore)",
                          gZelda3dStateCheck);
    } else if (strcmp(command, "scenescale") == 0 && sscanf(line, "%*s %f", &value) == 1) {
        gZelda3dSceneScale = value;
        Zelda3D_ReplReply(outPath, "scenescale=%.4f", gZelda3dSceneScale);
    } else if (strcmp(command, "hlroom") == 0) {
        if (sscanf(line, "%*s %i", &group) == 1) {
            gZelda3dHlGroup = group;
        }
        Zelda3D_ReplReply(outPath, "hlroom=%d", gZelda3dHlGroup);
    } else {
        return false;
    }
    return true;
}
