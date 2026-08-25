#include "render_tuning.h"

#include "../../anim/skeleton_draw_bridge.h"
#include "../../core/zelda3d_runtime.h"
#include "../../lighting/zelda3d_lighting.h"
#include "../../render/scene_tint.h"
#include "../../render/replacement_catalog.h"
#include "../zelda3d_repl.h"
#include <fast/zelda3d_render_control.h>

#include <stdio.h>
#include <string.h>

bool Zelda3D_RenderTuningReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    float first;
    float second;
    int value;

    if (strcmp(command, "mul") == 0 && sscanf(line, "%*s %f", &first) == 1) {
        gZelda3dTintMul = first;
        Zelda3D_ReplReply(outPath, "mul=%.3f", gZelda3dTintMul);
    } else if (strcmp(command, "diff") == 0 && sscanf(line, "%*s %f", &first) == 1) {
        gZelda3dTintDiff = first;
        Zelda3D_ReplReply(outPath, "diff=%.3f", gZelda3dTintDiff);
    } else if (strcmp(command, "tint") == 0 && sscanf(line, "%*s %f %f", &first, &second) == 2) {
        gZelda3dTintDiff = first;
        gZelda3dTintMul = second;
        Zelda3D_ReplReply(outPath, "diff=%.3f mul=%.3f", gZelda3dTintDiff, gZelda3dTintMul);
    } else if (strcmp(command, "enable") == 0 && sscanf(line, "%*s %f", &first) == 1) {
        gZelda3dEnabled = static_cast<int>(first);
        Zelda3D_ReplReply(outPath, "enabled=%d", gZelda3dEnabled);
    } else if (strcmp(command, "unified") == 0) {
        // Render-unification effort (kanban #131): 0=off (default) 1=CMB unified 2=N64 unified
        // 3=both. See gUnifiedRenderer (zelda3d_gl.cpp) for the full rationale.
        if (sscanf(line, "%*s %i", &value) == 1) {
            gUnifiedRenderer = value;
        }
        Zelda3D_ReplReply(outPath, "unified=%d", gUnifiedRenderer);
    } else if (strcmp(command, "facecull") == 0) {
        // Backface culling of OoT3D meshes (honor the CMB cull byte; matches N64 G_CULL_BACK so the
        // camera never sees terrain undersides / mesh interiors). `facecull <0|1> [flip]`: arg1 = on/off,
        // optional arg2 = front-face winding convention (0 default, 1 flipped — used to find the correct
        // winding live, since the backend's clip-Y handling decides whether CCW or CW is front).
        int on = -1;
        int flip = -1;
        if (sscanf(line, "%*s %d %d", &on, &flip) >= 1) {
            gZelda3dFaceCull = on;
            if (flip >= 0) {
                gZelda3dFaceCullFlip = flip;
            }
        }
        Zelda3D_ReplReply(outPath, "facecull=%d flip=%d", gZelda3dFaceCull, gZelda3dFaceCullFlip);
    } else if (strcmp(command, "state") == 0) {
        u8 tint[3];
        char scales[256];
        s32 length = 0;
        Zelda3D_SceneTint(play, tint);
        for (s32 i = 0; i < Zelda3D_ExplicitReplacementCount() && length < static_cast<s32>(sizeof(scales)) - 1; i++) {
            const Zelda3D_ModelEntry* entry = Zelda3D_ExplicitReplacementAt(i);
            if (entry == nullptr) {
                continue;
            }
            length += snprintf(scales + length, sizeof(scales) - length, "%s%s=%.4f(yoff %.0f)", i ? " " : "",
                               entry->name, entry->worldScale, entry->groundOffset);
        }
        Zelda3D_ReplReply(outPath,
                          "enabled=%d diff=%.3f mul=%.3f tint=(%d,%d,%d) anim(live=%d frame=%.1f rate=%.3f) "
                          "scale: %s",
                          Zelda3D_Enabled(), gZelda3dTintDiff, gZelda3dTintMul, tint[0], tint[1], tint[2],
                          gZelda3dAnimLive, gZelda3dAnimFrame, gZelda3dAnimRate, scales);
    } else {
        return false;
    }
    return true;
}
