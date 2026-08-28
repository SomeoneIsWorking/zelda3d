#pragma once

#include "2s2h/zelda3d/repl/mm3d_repl_command.h"
#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

// Dispatch the MM Link control commands (linkinfo, linkform, linkequip, linkitem, and linkstate).
// Returns nonzero when the command belongs to this module. All engine mutations remain owned by
// mm3d_player_force.
s32 Zelda3D_MmLinkReplDispatch(PlayState* play, const char* command, Zelda3DMmReplReply reply, void* user);

#ifdef __cplusplus
}
#endif
