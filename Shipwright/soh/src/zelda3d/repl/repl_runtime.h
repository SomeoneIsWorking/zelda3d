#ifndef ZELDA3D_REPL_RUNTIME_H
#define ZELDA3D_REPL_RUNTIME_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

// Internal dispatcher seam used by the FIFO transport.
void Zelda3D_ReplDispatchCommand(PlayState* play, char* line, const char* outPath);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_REPL_RUNTIME_H
