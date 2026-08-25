#include "2s2h/zelda3d/repl/mm3d_model_repl.h"

#include "2s2h/zelda3d/mm3d_model.h"

#include <stdio.h>

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
    return 0;
}
