#include "sky_control.h"

#include "../../render/sky_render.h"
#include "../../scene/sky_control.h"
#include "../zelda3d_repl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool Zelda3D_SkyControlReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    if (strcmp(command, "sky") != 0) {
        return false;
    }

    char subcommand[32];
    float scale;
    if (sscanf(line, "%*s scale %f", &scale) == 1) {
        gZelda3dSkyScale = scale;
    } else if (sscanf(line, "%*s %31s", subcommand) == 1 && strcmp(subcommand, "info") != 0) {
        gZelda3dSky = atoi(subcommand) != 0;
    }

    int activeIndex = Zelda3D_ActiveSkyIndex(play);
    int active = Zelda3D_SkyActive(play);
    int modelId = activeIndex >= 0 ? Zelda3D_SkyModelId(activeIndex) : -1;
    Zelda3D_ReplReply(outPath,
                      "sky=%d scale=%.2f skyboxId=%d idx1=%d idx2=%d blend=%d active=%d activeIdx=%d modelId=%d",
                      gZelda3dSky, gZelda3dSkyScale, play->skyboxId, play->envCtx.skybox1Index,
                      play->envCtx.skybox2Index, play->envCtx.skyboxBlend, active, activeIndex, modelId);
    return true;
}
