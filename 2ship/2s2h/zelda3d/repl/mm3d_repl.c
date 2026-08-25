#include "2s2h/zelda3d/repl/mm3d_repl.h"

#include "2s2h/zelda3d/repl/mm3d_framing_repl.h"
#include "2s2h/zelda3d/repl/mm3d_repl_router.h"
#include "2s2h/zelda3d/repl/mm3d_repl_transport.h"
#include "global.h"

static void Zelda3D_MmReplHandleCommand(const char* command, Zelda3DMmReplReply reply, void* replyUser, void* user) {
    Zelda3D_MmReplRouterDispatch((PlayState*)user, command, reply, replyUser);
}

void Zelda3D_MmReplTick(void) {
    Zelda3D_MmReplTransportPoll(Zelda3D_MmReplHandleCommand, gPlayState);
    Zelda3D_MmFramingReplApply(gPlayState);
}

void Zelda3D_MmReplResetRunState(void) {
    Zelda3D_MmReplTransportReset();
    Zelda3D_MmFramingReplReset();
}
