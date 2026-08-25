#include "player_probe_commands.h"

#include "frontend_input_commands.h"
#include "oracle_actor_commands.h"
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
    if (command == "actors") {
        HarnessOracle::HandleActors(arguments);
        // Preserve the legacy fallthrough to harness_repl's direct actor route.
        return false;
    }
    return false;
}

} // namespace HarnessPlayerProbe
