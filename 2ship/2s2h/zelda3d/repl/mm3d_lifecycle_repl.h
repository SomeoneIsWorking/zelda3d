#pragma once

#include "2s2h/zelda3d/repl/mm3d_repl_command.h"
#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_MmLifecycleReplDispatch(PlayState* play, const char* command, Zelda3DMmReplReply reply, void* user);

#ifdef __cplusplus
}
#endif
