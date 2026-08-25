#pragma once

#include "2s2h/zelda3d/repl/mm3d_repl_command.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*Zelda3DMmReplCommandHandler)(const char* command, Zelda3DMmReplReply reply, void* replyUser, void* user);

void Zelda3D_MmReplTransportReset(void);
void Zelda3D_MmReplTransportPoll(Zelda3DMmReplCommandHandler handler, void* user);

#ifdef __cplusplus
}
#endif
