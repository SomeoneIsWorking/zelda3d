#include "render_isolation.h"

#include "../../control/zelda3d_control_bridge.h"
#include "../../core/zelda3d_runtime.h"
#include "../../diagnostics/zelda3d_diagnostics.h"
#include "../zelda3d_repl.h"
#include <fast/zelda3d_instrumentation.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool Zelda3D_RenderIsolationReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    const char* cmd = command;
    char arg[64];
    char path[1024];
    float f1, f2, f3;
    int iv, iv2;
    if (strcmp(cmd, "sgdrawonly") == 0) {
        // Draw-isolation probe: render ONLY the n-th Zelda3D group of the frame (-1 = everything).
        // The point is draws whose pixels are entirely overlapped by later layers — Zora's water d9
        // has zero exclusive pixels, so no mask-restricted readback can attribute anything to it,
        // and FRAGDBG alone gives a whole-frame composite. Isolate the group and the frame IS that
        // draw's output, directly comparable with the oracle's per-fragment PIXEL probe.
        // Indices are per-frame, in append order; get them from `sgdrawlist`.
        if (sscanf(line, "%*s %i", &iv) == 1) {
            gZelda3dSgDrawOnly = iv;
        }
        Zelda3D_ReplReply(outPath, "sgdrawonly=%d", gZelda3dSgDrawOnly);
    } else if (strcmp(cmd, "sgdrawskip") == 0) {
        // Like the oracle harness's drawskip: retain the complete frame except for one native CMB
        // group. Full-vs-skipped pairs therefore measure the same composited contribution on both
        // engines; sgdrawlist supplies the per-frame index and its stable model-group/material ids.
        if (sscanf(line, "%*s %i", &iv) == 1) {
            gZelda3dSgDrawSkip = iv;
        }
        Zelda3D_ReplReply(outPath, "sgdrawskip=%d", gZelda3dSgDrawSkip);
    } else if (strcmp(cmd, "sgmodelonly") == 0) {
        // Stable counterpart to sgdrawonly: model ids do not shift when unrelated transient draws
        // enter/leave the frame, so multi-frame shader probes remain attached to their target.
        if (sscanf(line, "%*s %i", &iv) == 1)
            gZelda3dSgModelOnly = iv;
        Zelda3D_ReplReply(outPath, "sgmodelonly=%d", gZelda3dSgModelOnly);
    } else if (strcmp(cmd, "sgdrawlist") == 0) {
        // One-shot: dump the next frame's Zelda3D group list (index, model, vertex range, textures)
        // to stderr, so `sgdrawonly` has an index to aim at.
        gZelda3dSgDrawList = 1;
        Zelda3D_ReplReply(outPath, "sgdrawlist armed (one frame, to stderr/run.log)");
    } else if (strcmp(cmd, "sgdump") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // RenderDoc-style draw inspection: arm a one-shot dump of every material group's render state
        // for model <iv> on its next draw (-> stderr/run log, grep "SG_DUMP"). Diagnoses a missing or
        // invisible group by VALUE (which state kills it), not by eyeballing the frame.
        g_sgDumpModel = iv;
        Zelda3D_ReplReply(outPath, "sgdump armed for model %d (see run log: grep SG_DUMP)", iv);
    } else {
        return false;
    }
    return true;
}
