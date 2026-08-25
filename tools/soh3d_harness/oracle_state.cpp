#include "oracle_state.h"

#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"

namespace HarnessOracle {

std::optional<uint32_t> CurrentPlayState() {
    auto& memory = Core::System::GetInstance().Memory();
    const auto playState = memory.Read32OrNullopt(OracleLayout::kPlayStateAddress);
    if (playState && *playState != 0) {
        return *playState;
    }

    // The title demo is not a Play gamestate on 3DS. Its live PlayState pointer
    // resides in a separate fixed slot so scene and actor inspection can still
    // use the same typed state accessors while the demo is active.
    const auto titlePlayState = memory.Read32OrNullopt(OracleLayout::kTitlePlayStatePointerAddress);
    if (titlePlayState && *titlePlayState != 0) {
        return *titlePlayState;
    }
    return std::nullopt;
}

bool TitleActive() {
    auto& memory = Core::System::GetInstance().Memory();
    const auto gameplayPlayState = memory.Read32OrNullopt(OracleLayout::kPlayStateAddress);
    if (gameplayPlayState && *gameplayPlayState != 0) {
        return false;
    }

    const auto scene = memory.Read32OrNullopt(OracleLayout::kTitleContextAddress + OracleLayout::kTitleSceneOffset);
    const auto active = memory.Read32OrNullopt(OracleLayout::kTitleContextAddress + OracleLayout::kTitleActiveOffset);
    return scene && active && (*scene & 0xFFFF) == 0x51 && *active == 1;
}

std::optional<uint32_t> GameplayPlayState() {
    auto& memory = Core::System::GetInstance().Memory();
    const auto playState = memory.Read32OrNullopt(OracleLayout::kPlayStateAddress);
    if (!playState || *playState == 0 || TitleActive()) {
        return std::nullopt;
    }
    return *playState;
}

} // namespace HarnessOracle
