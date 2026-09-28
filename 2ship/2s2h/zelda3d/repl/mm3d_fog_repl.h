#pragma once

#include "2s2h/zelda3d/repl/mm3d_repl_command.h"
#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

// Dispatch the MM3D environment/fog diagnostics. Returns nonzero when the command belongs here.
//
// Why its own module rather than a command in mm3d_world_repl.c: the world's other commands are
// legacy C whose reply strings use snprintf, which this repository's clang-tidy configuration
// (clang-analyzer-security.insecureAPI) rejects outright. A new command added there would drag those
// pre-existing sites into the changed-file lint and fail the gate on code it did not touch. C++ with
// fmt is the idiom the tidied REPL modules already use (mm3d_link_repl.cpp), and it keeps this
// diagnostic's output honest without a hand-rolled fixed buffer.
s32 Zelda3D_MmFogReplDispatch(PlayState* play, const char* command, Zelda3DMmReplReply reply, void* user);

#ifdef __cplusplus
}
#endif
