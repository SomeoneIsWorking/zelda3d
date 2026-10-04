#pragma once

#include "2s2h/zelda3d/repl/mm3d_repl_command.h"
#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

// The `log` command: list MM's diagnostic channels, or turn one on and off in a running game.
int Zelda3D_MmLogReplDispatch(PlayState* play, const char* command, Zelda3DMmReplReply reply, void* user);

#ifdef __cplusplus
}
#endif