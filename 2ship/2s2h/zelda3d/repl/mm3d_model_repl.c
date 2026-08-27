#include "2s2h/zelda3d/repl/mm3d_model_repl.h"

#include "2s2h/zelda3d/mm3d_model.h"

#include <fast/zelda3d_instrumentation.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int Zelda3D_MmParseTraceModel(const char* token, int* modelId) {
    char* end = NULL;
    long parsed;
    if (strcmp(token, "off") == 0) {
        *modelId = -1;
        return 1;
    }
    errno = 0;
    parsed = strtol(token, &end, 0);
    if ((errno != 0) || (end == token) || (*end != '\0') || (parsed < 0) || (parsed > INT_MAX)) {
        return 0;
    }
    *modelId = (int)parsed;
    return 1;
}

int Zelda3D_MmModelReplDispatch(PlayState* play, const char* command, Zelda3DMmReplReply reply, void* user) {
    (void)play;
    Zelda3DMmReplArgs args;
    if (Zelda3D_MmReplMatch(command, "mscale", &args)) {
        int32_t objectId;
        float scale;
        if (!Zelda3D_MmReplParseI32(&args, 0, &objectId) || !Zelda3D_MmReplParseFloat(&args, &scale) ||
            !Zelda3D_MmReplArgsEnd(&args)) {
            reply("usage: mscale <objId> <scale>", user);
        } else {
            Zelda3D_SetObjectScale(objectId, scale);
            char output[96];
            snprintf(output, sizeof(output), "mscale obj=0x%03X scale=%.4f", objectId, scale);
            reply(output, user);
        }
        return 1;
    }
    if (Zelda3D_MmReplMatch(command, "mlist", &args)) {
        if (!Zelda3D_MmReplArgsEnd(&args)) {
            reply("usage: mlist", user);
        } else {
            Zelda3D_ListModels(reply, user);
        }
        return 1;
    }
    if (Zelda3D_MmReplMatch(command, "mptrace", &args)) {
        char target[32] = { 0 };
        int modelId;
        if (!Zelda3D_MmReplNextToken(&args, target, sizeof(target)) || !Zelda3D_MmReplArgsEnd(&args) ||
            !Zelda3D_MmParseTraceModel(target, &modelId)) {
            reply("usage: mptrace <modelId|off>", user);
        } else {
            gZelda3dTraceModelId = modelId;
            reply(modelId < 0 ? "mptrace disabled" : "mptrace enabled", user);
        }
        return 1;
    }
    return 0;
}
