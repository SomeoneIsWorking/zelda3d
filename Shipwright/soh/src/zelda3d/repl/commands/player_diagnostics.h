// Player, camera, climb, animation, and grounding diagnostics.
#ifndef ZELDA3D_REPL_COMMANDS_PLAYER_DIAGNOSTICS_H
#define ZELDA3D_REPL_COMMANDS_PLAYER_DIAGNOSTICS_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

s16 Zelda3D_CameraActiveFuncIdx(Camera* camera, const char** outName);

#ifdef __cplusplus
}
#endif

bool Zelda3D_PlayerDiagnosticsReplCommand(PlayState* play, const char* command, const char* line, const char* outPath);

#endif // ZELDA3D_REPL_COMMANDS_PLAYER_DIAGNOSTICS_H
