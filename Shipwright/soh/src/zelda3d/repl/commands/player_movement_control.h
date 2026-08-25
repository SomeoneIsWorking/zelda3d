// Live player movement, facing, and injected-input commands.
#ifndef ZELDA3D_REPL_COMMANDS_PLAYER_MOVEMENT_CONTROL_H
#define ZELDA3D_REPL_COMMANDS_PLAYER_MOVEMENT_CONTROL_H

#include "global.h"

bool Zelda3D_PlayerMovementControlReplCommand(PlayState* play, const char* command, const char* line,
                                              const char* outPath);

#endif // ZELDA3D_REPL_COMMANDS_PLAYER_MOVEMENT_CONTROL_H
