#include "player_probe_commands.h"

#include "frontend_input_commands.h"
#include "oracle_player_animation_commands.h"
#include "oracle_player_state_commands.h"
#include "soh_player_control_commands.h"
#include "soh_player_state_commands.h"

namespace HarnessPlayerProbe {

bool HandleCommand(const std::string& command, std::istringstream& arguments) {
    if (HarnessFrontendInput::HandleCommand(command, arguments) ||
        HarnessOraclePlayerAnimation::HandleCommand(command, arguments) ||
        HarnessOraclePlayerState::HandleCommand(command, arguments) ||
        HarnessSohPlayerControl::HandleCommand(command, arguments) ||
        HarnessSohPlayerState::HandleCommand(command, arguments)) {
        return true;
    }
    return false;
}

} // namespace HarnessPlayerProbe
