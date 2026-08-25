// Forced player action-state and climb commands.
#ifndef ZELDA3D_REPL_COMMANDS_PLAYER_ACTION_CONTROL_H
#define ZELDA3D_REPL_COMMANDS_PLAYER_ACTION_CONTROL_H

#include "global.h"

bool Zelda3D_PlayerActionControlReplCommand(PlayState* play, const char* command, const char* line,
                                            const char* outPath);

#endif // ZELDA3D_REPL_COMMANDS_PLAYER_ACTION_CONTROL_H
