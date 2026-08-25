#include "oracle_player_locator.h"

#include "actor_layout.h"
#include "core/core.h"
#include "core/memory.h"
#include "oracle_state.h"

namespace HarnessOraclePlayer {

std::optional<uint32_t> FindActor() {
    const auto playState = HarnessOracle::CurrentPlayState();
    if (!playState) {
        return std::nullopt;
    }
    auto& memory = Core::System::GetInstance().Memory();
    const auto head = memory.Read32OrNullopt(ActorLayout::ListAddress(*playState, 2) + ActorLayout::kListHeadOffset);
    if (!head || *head == 0) {
        return std::nullopt;
    }
    return *head;
}

} // namespace HarnessOraclePlayer
