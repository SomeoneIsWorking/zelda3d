#include "actor_cucco_control.h"

#include "../../behaviors/actor/cucco_control.h"
#include "../../behaviors/actor/cucco_wing_override.h"
#include "../zelda3d_repl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool Zelda3D_ActorCuccoReplCommand(const char* command, const char* line, const char* outPath) {
    if (strcmp(command, "wingflap") == 0) {
        char sub[32];
        int value;
        if (sscanf(line, "%*s force %d", &value) == 1) {
            gZelda3dWingForce = value;
        } else if (sscanf(line, "%*s %31s", sub) == 1) {
            gZelda3dProcOverride = (atoi(sub) != 0);
        }
        Zelda3D_ReplReply(outPath, "wingflap=%d force=%d", gZelda3dProcOverride, gZelda3dWingForce);
    } else if (strcmp(command, "cuccopose") == 0) {
        int value;
        if (sscanf(line, "%*s %d", &value) == 1) {
            gZelda3dForceCuccoAgitate = (value != 0);
            gZelda3dCuccoState = value ? 2 : -1;
        }
        Zelda3D_ReplReply(outPath, "cuccopose=%d (cuccostate=%d). Hold still+frame via asel/afreeze/acam.",
                          gZelda3dForceCuccoAgitate, gZelda3dCuccoState);
    } else if (strcmp(command, "cuccostate") == 0) {
        char sub[16];
        if (sscanf(line, "%*s %15s", sub) == 1) {
            gZelda3dCuccoState = (strcmp(sub, "off") == 0) ? -1 : atoi(sub);
        }
        Zelda3D_ReplReply(outPath, "cuccostate=%d (-1=live AI)", gZelda3dCuccoState);
    } else if (strcmp(command, "cuccoheld") == 0) {
        int value;
        if (sscanf(line, "%*s %d", &value) == 1) {
            gZelda3dCuccoHeld = (value != 0);
        }
        Zelda3D_ReplReply(outPath, "cuccoheld=%d (pair with afreeze 2)", gZelda3dCuccoHeld);
    } else if (strcmp(command, "flapinfo") == 0) {
        Zelda3D_ReplReply(outPath, "flapinfo state=%d phase=%d limb7=(%d,%d,%d) limb11=(%d,%d,%d)", gZelda3dCuccoState,
                          gZelda3dCuccoDbgPhase, gZelda3dCuccoDbgWing[0], gZelda3dCuccoDbgWing[1],
                          gZelda3dCuccoDbgWing[2], gZelda3dCuccoDbgWing[3], gZelda3dCuccoDbgWing[4],
                          gZelda3dCuccoDbgWing[5]);
    } else if (strcmp(command, "wingprobe") == 0) {
        int x;
        int y;
        int z;
        if (sscanf(line, "%*s %d %d %d", &x, &y, &z) == 3) {
            gZelda3dWingProbe[0] = x;
            gZelda3dWingProbe[1] = y;
            gZelda3dWingProbe[2] = z;
            gZelda3dWingProbeActive = 1;
        } else {
            gZelda3dWingProbeActive = 0;
        }
        Zelda3D_ReplReply(outPath, "wingprobe active=%d xyz=(%d,%d,%d)", gZelda3dWingProbeActive, gZelda3dWingProbe[0],
                          gZelda3dWingProbe[1], gZelda3dWingProbe[2]);
    } else if (strcmp(command, "bonerot") == 0) {
        char sub[16];
        int boneId;
        int x;
        int y;
        int z;
        if (sscanf(line, "%*s %15s", sub) == 1 && strcmp(sub, "off") == 0) {
            gZelda3dDbgBone = -1;
        } else if (sscanf(line, "%*s %d %d %d %d", &boneId, &x, &y, &z) == 4) {
            gZelda3dDbgBone = boneId;
            gZelda3dDbgBoneRot[0] = x;
            gZelda3dDbgBoneRot[1] = y;
            gZelda3dDbgBoneRot[2] = z;
        }
        Zelda3D_ReplReply(outPath, "bonerot bone=%d xyz=(%d,%d,%d)", gZelda3dDbgBone, gZelda3dDbgBoneRot[0],
                          gZelda3dDbgBoneRot[1], gZelda3dDbgBoneRot[2]);
    } else if (strcmp(command, "wingmap") == 0) {
        char sub[16];
        int sourceX;
        int sourceY;
        int sourceZ;
        int signX;
        int signY;
        int signZ;
        if (sscanf(line, "%*s %15s", sub) == 1 && strcmp(sub, "off") == 0) {
            gZelda3dWingMapSrc[0] = gZelda3dWingMapSrc[1] = gZelda3dWingMapSrc[2] = -1;
        } else if (sscanf(line, "%*s %d %d %d %d %d %d", &sourceX, &sourceY, &sourceZ, &signX, &signY, &signZ) == 6) {
            if (sourceX > 2 || sourceY > 2 || sourceZ > 2 || sourceX < -1 || sourceY < -1 || sourceZ < -1) {
                Zelda3D_ReplReply(outPath,
                                  "wingmap REFUSED src=(%d,%d,%d) -- each source axis must be "
                                  "-1 (none), 0 (x), 1 (y) or 2 (z); nothing was changed",
                                  sourceX, sourceY, sourceZ);
                return true;
            }
            gZelda3dWingMapSrc[0] = sourceX;
            gZelda3dWingMapSrc[1] = sourceY;
            gZelda3dWingMapSrc[2] = sourceZ;
            gZelda3dWingMapSign[0] = signX;
            gZelda3dWingMapSign[1] = signY;
            gZelda3dWingMapSign[2] = signZ;
        }
        Zelda3D_ReplReply(outPath, "wingmap src=(%d,%d,%d) sign=(%d,%d,%d) %s", gZelda3dWingMapSrc[0],
                          gZelda3dWingMapSrc[1], gZelda3dWingMapSrc[2], gZelda3dWingMapSign[0], gZelda3dWingMapSign[1],
                          gZelda3dWingMapSign[2], gZelda3dWingMapSrc[0] < 0 ? "(table)" : "(live)");
    } else if (strcmp(command, "chickflap") == 0) {
        char sub[16];
        int value;
        float frequency;
        if (sscanf(line, "%*s axis %d", &value) == 1) {
            gZelda3dChickAxis = value;
        } else if (sscanf(line, "%*s center %d", &value) == 1) {
            gZelda3dChickCenter = value;
        } else if (sscanf(line, "%*s amp %d", &value) == 1) {
            gZelda3dChickAmp = value;
        } else if (sscanf(line, "%*s freq %f", &frequency) == 1) {
            gZelda3dChickFreq = frequency;
        } else if (sscanf(line, "%*s mirror %d", &value) == 1) {
            gZelda3dChickBone2Sign = value;
        } else if (sscanf(line, "%*s %15s", sub) == 1) {
            gZelda3dChickFlap = (atoi(sub) != 0);
        }
        Zelda3D_ReplReply(outPath, "chickflap=%d axis=%d center=%d amp=%d freq=%.2f mirror=%d", gZelda3dChickFlap,
                          gZelda3dChickAxis, gZelda3dChickCenter, gZelda3dChickAmp, gZelda3dChickFreq,
                          gZelda3dChickBone2Sign);
    } else {
        return false;
    }
    return true;
}
