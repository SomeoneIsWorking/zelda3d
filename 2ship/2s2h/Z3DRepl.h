#ifndef Z3D_REPL_H
#define Z3D_REPL_H

// Z3DRepl — the MINIMAL per-game REPL for native MM (2s2h). It handles ONLY the queries that need
// MM's `PlayState` and decomp types (scene/room/Link pose, live warp, actor scan). INPUT does NOT
// live here: synthetic buttons/stick go through the shared, game-agnostic libultraship path
// (ScriptedInput + ScriptedInputFifo), so both games share one input seam. This keeps the per-game
// surface tiny, exactly as the MM_NATIVE.md N3.4 phase-2b design calls for.
//
// OFF by default: it opens its FIFO only when env ZELDA3D_MM_REPL=<path> is set; replies go to
// "<path>.out". Ticked once per frame from GameState_Update.
//
// Command grammar (one per line):
//   posinfo             -> "scene=<id> room=<n> pos=(x,y,z) rot=<yaw>"
//   warp <entrance>     live scene transition to <entrance> (strtol base 0)
//   actors [n]          per-category live actor counts; with n, the n nearest actors to Link
//   ping                -> "pong"

void Z3D_Repl_Tick(void);

#endif // Z3D_REPL_H
