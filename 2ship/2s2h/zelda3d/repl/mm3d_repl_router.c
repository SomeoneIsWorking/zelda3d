#include "2s2h/zelda3d/repl/mm3d_repl_router.h"

#include "2s2h/zelda3d/repl/mm3d_framing_repl.h"
#include "2s2h/zelda3d/repl/mm3d_lifecycle_repl.h"
#include "2s2h/zelda3d/repl/mm3d_link_repl.h"
#include "2s2h/zelda3d/repl/mm3d_model_repl.h"
#include "2s2h/zelda3d/repl/mm3d_scene_repl.h"
#include "2s2h/zelda3d/repl/mm3d_world_repl.h"

void Zelda3D_MmReplRouterDispatch(PlayState* play, const char* command, Zelda3DMmReplReply reply, void* user) {
    if (Zelda3D_MmLinkReplDispatch(play, command, reply, user) ||
        Zelda3D_MmWorldReplDispatch(play, command, reply, user) ||
        Zelda3D_MmSceneReplDispatch(play, command, reply, user) ||
        Zelda3D_MmFramingReplDispatch(play, command, reply, user) ||
        Zelda3D_MmModelReplDispatch(play, command, reply, user) ||
        Zelda3D_MmLifecycleReplDispatch(play, command, reply, user)) {
        return;
    }

    Zelda3DMmReplArgs args;
    if (Zelda3D_MmReplMatch(command, "ping", &args)) {
        reply(Zelda3D_MmReplArgsEnd(&args) ? "pong" : "usage: ping", user);
    } else {
        reply("err unknown-command", user);
    }
}
