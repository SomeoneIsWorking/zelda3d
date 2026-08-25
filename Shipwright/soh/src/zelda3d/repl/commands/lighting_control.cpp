#include "lighting_control.h"

#include "../../lighting/zelda3d_lighting.h"
#include "../../render/scene_lighting_submission.h"
#include "../zelda3d_repl.h"
#include <fast/zelda3d_lighting.h>
#include <fast/zelda3d_render_control.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

bool Zelda3D_LightingControlReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    int value;

    if (strcmp(command, "lightdir") == 0) {
        // `lightdir x y z` overrides the world-space form-light dir (held until `lightdir auto`);
        // `lightdir auto` returns to the scene's live light1Dir; `lightdir` alone prints the dir.
        float direction[3];
        char subcommand[32];
        if (sscanf(line, "%*s %f %f %f", &direction[0], &direction[1], &direction[2]) == 3) {
            float length =
                sqrtf(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
            if (length < 1e-4f) {
                length = 1.0f;
            }
            direction[0] /= length;
            direction[1] /= length;
            direction[2] /= length;
            gZelda3dLightDirOverride = 1;
            gZelda3dLightDirLast[0] = direction[0];
            gZelda3dLightDirLast[1] = direction[1];
            gZelda3dLightDirLast[2] = direction[2];
            Zelda3D_GL_SetLightDir(direction);
            Zelda3D_ReplReply(outPath, "lightdir OVERRIDE=(%.3f,%.3f,%.3f)", direction[0], direction[1], direction[2]);
        } else if (sscanf(line, "%*s %31s", subcommand) == 1 && strcmp(subcommand, "auto") == 0) {
            gZelda3dLightDirOverride = 0;
            Zelda3D_ReplReply(outPath, "lightdir AUTO (scene light1Dir)");
        } else {
            Zelda3D_ReplReply(outPath, "lightdir=(%.3f,%.3f,%.3f) %s", gZelda3dLightDirLast[0], gZelda3dLightDirLast[1],
                              gZelda3dLightDirLast[2],
                              gZelda3dLightDirOverride ? "(override)" : "(auto/live light1Dir)");
        }
    } else if (strcmp(command, "lightparams") == 0) {
        // Print both calibration feeds so palette mixing is visible at a glance.
        EnvLightSettings* n64Settings = play != nullptr ? &play->envCtx.lightSettings : nullptr;
        Zelda3D_ReplReply(
            outPath,
            "lightparams: ambient=(%.3f,%.3f,%.3f) light1col=(%.3f,%.3f,%.3f) "
            "light1dir=(%.3f,%.3f,%.3f) light2dir=(%.3f,%.3f,%.3f) light2col=(%.3f,%.3f,%.3f) | "
            "envColorsValid=%d | n64rows: amb=(%d,%d,%d) l1=(%d,%d,%d) l2=(%d,%d,%d)",
            gZelda3dAmbient[0], gZelda3dAmbient[1], gZelda3dAmbient[2], gZelda3dLight1Col[0], gZelda3dLight1Col[1],
            gZelda3dLight1Col[2], gZelda3dLightDirLast[0], gZelda3dLightDirLast[1], gZelda3dLightDirLast[2],
            gZelda3dLight2Dir[0], gZelda3dLight2Dir[1], gZelda3dLight2Dir[2], gZelda3dLight2Col[0],
            gZelda3dLight2Col[1], gZelda3dLight2Col[2], static_cast<int>(gZelda3dEnvColors.valid),
            n64Settings ? n64Settings->ambientColor[0] : -1, n64Settings ? n64Settings->ambientColor[1] : -1,
            n64Settings ? n64Settings->ambientColor[2] : -1, n64Settings ? n64Settings->light1Color[0] : -1,
            n64Settings ? n64Settings->light1Color[1] : -1, n64Settings ? n64Settings->light1Color[2] : -1,
            n64Settings ? n64Settings->light2Color[0] : -1, n64Settings ? n64Settings->light2Color[1] : -1,
            n64Settings ? n64Settings->light2Color[2] : -1);
    } else if (strcmp(command, "worldlit") == 0) {
        if (sscanf(line, "%*s %i", &value) == 1) {
            gZelda3dWorldLit = value;
        }
        Zelda3D_ReplReply(outPath, "worldlit=%d", gZelda3dWorldLit);
    } else if (strcmp(command, "worldamb") == 0) {
        float coefficient;
        float red;
        float green;
        float blue;
        if (sscanf(line, "%*s %f %f %f %f", &coefficient, &red, &green, &blue) == 4) {
            gZelda3dWorldAmb = coefficient;
            gZelda3dWorldAmbColor[0] = red;
            gZelda3dWorldAmbColor[1] = green;
            gZelda3dWorldAmbColor[2] = blue;
            gZelda3dWorldAmbOverride = 1;
        } else if (sscanf(line, "%*s %f", &coefficient) == 1) {
            gZelda3dWorldAmb = coefficient;
        }
        Zelda3D_ReplReply(outPath, "worldamb=%.3f ambColor=(%.3f,%.3f,%.3f) override=%d", gZelda3dWorldAmb,
                          gZelda3dWorldAmbColor[0], gZelda3dWorldAmbColor[1], gZelda3dWorldAmbColor[2],
                          gZelda3dWorldAmbOverride);
    } else {
        return false;
    }
    return true;
}
