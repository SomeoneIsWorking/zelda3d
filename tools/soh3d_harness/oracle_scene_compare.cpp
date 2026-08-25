#include "oracle_scene_compare.h"

#include <cstdio>

#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "oracle_state.h"
#include "oracle_title_state.h"
#include "soh_play_state.h"

namespace HarnessOracle {

void CompareSceneImpl() {
    const auto playState = CurrentPlayState();
    if (playState) {
        const auto scene =
            Core::System::GetInstance().Memory().Read32OrNullopt(*playState + OracleLayout::kSceneNumberOffset);
        std::printf("  3ds: sceneNum=0x%04x\n", scene ? static_cast<unsigned>(*scene & 0xFFFF) : 0xFFFFU);
    } else if (TitleActive()) {
        std::printf("  3ds: sceneNum=0x0051 (title, inline ctx @ 0x%08x)\n", OracleLayout::kTitleContextAddress);
    } else {
        std::printf("  3ds: n/a (no playstate, no title)\n");
    }

    if (!SohState_HasPlayState()) {
        std::printf("  soh: n/a (no playstate)\n");
    } else {
        std::printf("  soh: sceneNum=0x%04x roomNum=%d\n", static_cast<unsigned>(SohState_SceneNum() & 0xFFFF),
                    SohState_RoomNum());
    }
}

} // namespace HarnessOracle
