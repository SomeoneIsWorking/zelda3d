// Zelda3D REPL — interactive control of a long-lived headless instance. Extracted out of
// zelda3d.c (Phase 2b codebase reorg, see docs/codemap.md) into zelda3d/repl/zelda3d_repl.cpp:
// Zelda3D_ReplReply, the ~149-command Zelda3D_ReplExec dispatcher, and the per-frame
// Zelda3D_ReplPoll (called once per frame from z_play.c's Play_Update). Both entry points are
// also declared in zelda3d.h (their original home, unchanged) — this header exists so a future
// consumer of the REPL module specifically doesn't need to pull in the whole zelda3d.h umbrella
// just to find them, and so this module's dependency on zelda3d/render/zelda3d_render.h (the
// render-owned state ~149 of the commands poke) is documented in one place.
#ifndef ZELDA3D_REPL_H
#define ZELDA3D_REPL_H

#include "global.h"
#include "render/zelda3d_render.h"

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_ReplReply(const char* outPath, const char* fmt, ...); // REPL reply line (stdout + .out)
void Zelda3D_ReplPoll(PlayState* play); // per-frame: drains the control FIFO, applies live overrides

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_REPL_H
