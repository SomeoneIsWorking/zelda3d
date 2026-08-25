#include "player_control.h"

#include "player_action_control.h"
#include "player_diagnostics.h"
#include "player_movement_control.h"
#include "player_pause_control.h"

bool Zelda3D_PlayerControlReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    return Zelda3D_PlayerMovementControlReplCommand(play, command, line, outPath) ||
           Zelda3D_PlayerPauseControlReplCommand(play, command, line, outPath) ||
           Zelda3D_PlayerActionControlReplCommand(play, command, line, outPath) ||
           Zelda3D_PlayerDiagnosticsReplCommand(play, command, line, outPath);
}
