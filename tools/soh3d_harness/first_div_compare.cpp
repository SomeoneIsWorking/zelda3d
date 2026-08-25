#include "first_div_compare.h"

#include "first_div_gameplay_compare.h"
#include "first_div_reporter.h"
#include "first_div_state_capture.h"
#include "first_div_title_camera_compare.h"
#include "first_div_title_pose_compare.h"

#include <cstdio>

namespace HarnessOracle {
namespace {

void CompareEngineMode(const FirstDivEngineState& state, FirstDivReporter& reporter) {
    std::printf("  d1 gamestate:    az=%s soh=%s\n",
                state.oracleAtTitle  ? "title(inline)"
                : state.oracleInPlay ? "play"
                                     : "n/a",
                state.sohAtTitle  ? "play(scene=0x51)"
                : state.sohInPlay ? "play"
                                  : "not-title");
    if (state.oracleAtTitle != state.sohAtTitle && !(state.oracleInPlay && state.sohInPlay)) {
        char details[128];
        std::snprintf(details, sizeof(details),
                      "az_title=%d soh_title=%d az_play=%d soh_play=%d — engines in different gamestate machinery",
                      static_cast<int>(state.oracleAtTitle), static_cast<int>(state.sohAtTitle),
                      static_cast<int>(state.oracleInPlay), static_cast<int>(state.sohInPlay));
        reporter.Report("gamestate-mode", details);
    }
}

void CompareSceneNumber(const FirstDivEngineState& state, FirstDivReporter& reporter) {
    std::printf("  d2 sceneNum:     az=0x%04x soh=0x%04x\n", state.oracleScene, state.sohScene);
    if (!reporter.Reported() && state.oracleScene != state.sohScene) {
        char details[128];
        std::snprintf(details, sizeof(details), "az=0x%04x soh=0x%04x", state.oracleScene, state.sohScene);
        reporter.Report("sceneNum", details);
    }
}

} // namespace

void CompareFirstDivImpl() {
    FirstDivReporter reporter;
    const FirstDivEngineState state = CaptureFirstDivEngineState();
    CompareEngineMode(state, reporter);
    CompareSceneNumber(state, reporter);

    if (state.BothInSameGameplayScene()) {
        CompareGameplayFirstDiv(*state.oraclePlayState, reporter);
        return;
    }

    CompareTitlePoseFirstDiv(state.oracleAtTitle, state.sohAtTitle, reporter);
    CompareTitleCameraFirstDiv(state.oracleAtTitle, state.sohAtTitle, reporter);
    if (!reporter.Reported()) {
        std::printf("  firstdiv: none — all 5 checked dimensions matched\n");
    }
}

} // namespace HarnessOracle
