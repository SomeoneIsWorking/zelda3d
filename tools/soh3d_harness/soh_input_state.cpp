#include "soh_input_state.h"

#include "global.h"

namespace {

bool g_overrideActive = false;
u16 g_button = 0;
s8 g_stickX = 0;
s8 g_stickY = 0;
OSContPad g_previous{};

int ApplyDeadzone(int coordinate) {
    if (coordinate > 7) {
        return coordinate < 0x43 ? coordinate - 7 : 0x43 - 7;
    }
    if (coordinate < -7) {
        return coordinate > -0x43 ? coordinate + 7 : -(0x43 - 7);
    }
    return 0;
}

} // namespace

extern "C" {

int SohState_SetInput(unsigned int button, int stickX, int stickY) {
    if (gPlayState == nullptr) {
        return 0;
    }
    g_overrideActive = true;
    g_button = static_cast<u16>(button & 0xFFFF);
    g_stickX = static_cast<s8>(stickX);
    g_stickY = static_cast<s8>(stickY);
    return 1;
}

int SohState_ClearInputOverride(void) {
    g_overrideActive = false;
    g_button = 0;
    g_stickX = 0;
    g_stickY = 0;
    g_previous = {};
    return 1;
}

int SohState_ApplyInputOverride(void* input0) {
    if (!g_overrideActive) {
        return 0;
    }
    Input& input = *static_cast<Input*>(input0);
    input.cur.button = g_button;
    input.cur.stick_x = g_stickX;
    input.cur.stick_y = g_stickY;
    input.press.button = static_cast<u16>((input.cur.button ^ g_previous.button) & input.cur.button);
    input.rel.button = static_cast<u16>((input.cur.button ^ g_previous.button) & g_previous.button);
    input.prev = g_previous;
    g_previous = input.cur;
    input.rel.stick_x = static_cast<s8>(ApplyDeadzone(static_cast<int>(g_stickX)));
    input.rel.stick_y = static_cast<s8>(ApplyDeadzone(static_cast<int>(g_stickY)));
    return 1;
}

} // extern "C"
