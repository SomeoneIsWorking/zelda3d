#include "oracle_actor_commands.h"

#include "actor_compare.h"
#include "oracle_state.h"
#include "repl_protocol.h"

namespace HarnessOracle {

void HandleActors(std::istringstream&) {
    const auto playState = CurrentPlayState();
    if (!playState) {
        HarnessRepl::PrintErr("actors: no playstate");
        return;
    }
    DumpOracleActors(*playState);
}

} // namespace HarnessOracle
