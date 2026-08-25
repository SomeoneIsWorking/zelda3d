#include "fog_control.h"

#include "../../render/fog_render.h"
#include "../zelda3d_repl.h"
#include <fast/zelda3d_fog.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool Zelda3D_FogControlReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    int value;
    if (strcmp(command, "fog3d") == 0 && sscanf(line, "%*s %i", &value) == 1) {
        // A/B latch for the OoT3D PICA distance-fog port. Diagnostic only; game code never sets it.
        gZelda3dFog3dForceOff = value == 0 ? 1 : 0;
        Zelda3D_ReplReply(outPath, "fog3d %s (forceOff=%d)", value ? "on" : "OFF", gZelda3dFog3dForceOff);
    } else if (strcmp(command, "fog") == 0) {
        EnvLightSettings* settings = &play->envCtx.lightSettings;
        char subcommand[32];
        float first;
        float second;
        float third;
        if (sscanf(line, "%*s pos %f %f", &first, &second) == 2) {
            gZelda3dFogOverride = 1;
            Zelda3D_FogSetPosition(first, second);
        } else if (sscanf(line, "%*s pos %f", &first) == 1) {
            gZelda3dFogOverride = 1;
            Zelda3D_FogSetPosition(first, 1000.0f);
        } else if (sscanf(line, "%*s color %f %f %f", &first, &second, &third) == 3) {
            gZelda3dFogOverride = 1;
            gZelda3dFogColor[0] = first / 255.0f;
            gZelda3dFogColor[1] = second / 255.0f;
            gZelda3dFogColor[2] = third / 255.0f;
        } else if (sscanf(line, "%*s %31s", subcommand) == 1 && strcmp(subcommand, "info") != 0) {
            if (strcmp(subcommand, "auto") == 0) {
                gZelda3dFogOverride = 0;
            } else {
                gZelda3dFogEnable = atoi(subcommand) != 0;
            }
        }
        Zelda3D_ReplReply(outPath,
                          "fog=%d override=%d color=(%.0f,%.0f,%.0f) mul=%.0f offset=%.0f | env: "
                          "fogColor=(%d,%d,%d) fogNear=%d fogFar=%d",
                          gZelda3dFogEnable, gZelda3dFogOverride, gZelda3dFogColor[0] * 255, gZelda3dFogColor[1] * 255,
                          gZelda3dFogColor[2] * 255, gZelda3dFogMul, gZelda3dFogOffset, settings->fogColor[0],
                          settings->fogColor[1], settings->fogColor[2], settings->fogNear, settings->fogFar);
    } else {
        return false;
    }
    return true;
}
