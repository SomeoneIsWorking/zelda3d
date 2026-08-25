#include "first_div_state_capture.h"

#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "oracle_state.h"
#include "soh_play_state.h"

namespace HarnessOracle {

bool FirstDivEngineState::BothInSameGameplayScene() const {
    return oracleInPlay && sohInPlay && oracleScene != 0xFFFF && oracleScene != 0x51 && oracleScene == sohScene;
}

FirstDivEngineState CaptureFirstDivEngineState() {
    FirstDivEngineState state;
    state.oracleAtTitle = TitleActive();
    state.sohAtTitle = SohState_HasPlayState() && (SohState_SceneNum() & 0xFFFF) == 0x51;
    state.sohInPlay = SohState_HasPlayState();
    state.oraclePlayState = CurrentPlayState();
    state.oracleInPlay = !state.oracleAtTitle && state.oraclePlayState.has_value();

    if (state.oracleAtTitle) {
        state.oracleScene = 0x51;
    } else if (state.oraclePlayState) {
        const auto scene = Core::System::GetInstance().Memory().Read32OrNullopt(*state.oraclePlayState +
                                                                                OracleLayout::kSceneNumberOffset);
        if (scene) {
            state.oracleScene = *scene & 0xFFFF;
        }
    }
    if (state.sohInPlay) {
        state.sohScene = SohState_SceneNum() & 0xFFFF;
    }
    return state;
}

} // namespace HarnessOracle
