#include "actor_model_overrides.h"

#include "../../render/actor_skin_mask_control.h"
#include "../../diagnostics/model_tuning.h"
#include "../zelda3d_repl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

void ApplyKokiriMaskOverride(const char* line) {
    char argument[32] = {};
    int meshId = 0;
    if (sscanf(line, "%*s %31s", argument) != 1) {
        return;
    }
    if (strcmp(argument, "auto") == 0) {
        gZelda3dEnKoMaskOverrideSet = 0;
    } else if (strcmp(argument, "all") == 0) {
        gZelda3dEnKoMaskOverride = ~0ull;
        gZelda3dEnKoMaskOverrideSet = 1;
    } else if (strcmp(argument, "only") == 0 && sscanf(line, "%*s %*s %d", &meshId) == 1) {
        gZelda3dEnKoMaskOverride = (meshId >= 0 && meshId < 64) ? (1ull << meshId) : 0ull;
        gZelda3dEnKoMaskOverrideSet = 1;
    } else if (strcmp(argument, "add") == 0 && sscanf(line, "%*s %*s %d", &meshId) == 1) {
        if (meshId >= 0 && meshId < 64) {
            gZelda3dEnKoMaskOverride |= (1ull << meshId);
        }
        gZelda3dEnKoMaskOverrideSet = 1;
    } else if (strcmp(argument, "del") == 0 && sscanf(line, "%*s %*s %d", &meshId) == 1) {
        if (meshId >= 0 && meshId < 64) {
            gZelda3dEnKoMaskOverride &= ~(1ull << meshId);
        }
        gZelda3dEnKoMaskOverrideSet = 1;
    } else {
        gZelda3dEnKoMaskOverride = strtoull(argument, nullptr, 0);
        gZelda3dEnKoMaskOverrideSet = 1;
    }
}

} // namespace

bool Zelda3D_ActorModelOverridesReplCommand(const char* command, const char* line, const char* outPath) {
    if (strcmp(command, "enkomask") == 0) {
        ApplyKokiriMaskOverride(line);
        Zelda3D_ReplReply(outPath, "enkomask override=%s mask=0x%llx", gZelda3dEnKoMaskOverrideSet ? "ON" : "OFF(auto)",
                          gZelda3dEnKoMaskOverride);
    } else if (strcmp(command, "gscale") == 0) {
        int modelId;
        float scale;
        if (sscanf(line, "%*s %i %f", &modelId, &scale) != 2) {
            return false;
        }
        if (modelId >= 0 && modelId < 32) {
            gZelda3dGScale[modelId] = scale;
            Zelda3D_ReplReply(outPath, "gscale[%d]=%.4f%s", modelId, scale, scale <= 0.0f ? " (default)" : "");
        } else {
            Zelda3D_ReplReply(outPath, "gscale: id out of range (0..31)");
        }
    } else {
        return false;
    }
    return true;
}
