#include "pause_navigation.h"

namespace {

constexpr int kNoTarget = -1;
constexpr int kCloseTarget = -2;
constexpr int kPauseOpenState = 6;

int sTarget = kNoTarget;

void PressButton(Input* input, u16 button) {
    input->cur.button |= button;
    input->press.button |= button;
}

} // namespace

extern "C" void Zelda3D_PauseNavigationSetTarget(int target) {
    sTarget = target;
}

extern "C" int Zelda3D_PauseNavigationTarget(void) {
    return sTarget;
}

extern "C" void Zelda3D_PauseNavigationInject(PlayState* play) {
    if (play == nullptr || sTarget == kNoTarget) {
        return;
    }

    PauseContext* pauseContext = &play->pauseCtx;
    Input* input = &play->state.input[0];
    if (sTarget == kCloseTarget) {
        if (pauseContext->state == 0) {
            sTarget = kNoTarget;
        } else if (pauseContext->state == kPauseOpenState && pauseContext->unk_1E4 == 0) {
            PressButton(input, BTN_START);
        }
    } else if (pauseContext->state == 0) {
        PressButton(input, BTN_START);
    } else if (pauseContext->state == kPauseOpenState && pauseContext->unk_1E4 == 0) {
        if (pauseContext->pageIndex == static_cast<u16>(sTarget)) {
            sTarget = kNoTarget;
        } else {
            PressButton(input, BTN_R);
        }
    }
}
