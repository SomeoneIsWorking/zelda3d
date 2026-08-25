#include "frontend_input.h"

#include <cstdio>

#include "libretro.h"
#include "repl_protocol.h"

namespace HarnessFrontend {

namespace {

uint32_t g_input_mask = 0;
uint64_t g_input_poll_count = 0;
uint32_t g_input_poll_ids_seen = 0;
int16_t g_analog_left_x = 0;
int16_t g_analog_left_y = 0;
int16_t g_analog_right_x = 0;
int16_t g_analog_right_y = 0;

} // namespace

void InputPoll() {}

int16_t InputState(unsigned /*port*/, unsigned device, unsigned index, unsigned id) {
    if (device == RETRO_DEVICE_ANALOG) {
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT) {
            if (id == RETRO_DEVICE_ID_ANALOG_X) {
                return g_analog_left_x;
            }
            if (id == RETRO_DEVICE_ID_ANALOG_Y) {
                return g_analog_left_y;
            }
        } else if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
            if (id == RETRO_DEVICE_ID_ANALOG_X) {
                return g_analog_right_x;
            }
            if (id == RETRO_DEVICE_ID_ANALOG_Y) {
                return g_analog_right_y;
            }
        }
        return 0;
    }
    if (device != RETRO_DEVICE_JOYPAD || id >= 32) {
        return 0;
    }
    ++g_input_poll_count;
    g_input_poll_ids_seen |= 1U << id;
    return (g_input_mask >> id) & 1U;
}

uint32_t InputMask() {
    return g_input_mask;
}

void SetInputMask(uint32_t mask) {
    g_input_mask = mask;
}

uint64_t InputPollCount() {
    return g_input_poll_count;
}

uint32_t InputIdsSeen() {
    return g_input_poll_ids_seen;
}

void SetAnalog(int16_t leftX, int16_t leftY, int16_t rightX, int16_t rightY) {
    g_analog_left_x = leftX;
    g_analog_left_y = leftY;
    g_analog_right_x = rightX;
    g_analog_right_y = rightY;
}

void GetAnalog(int16_t* leftX, int16_t* leftY, int16_t* rightX, int16_t* rightY) {
    *leftX = g_analog_left_x;
    *leftY = g_analog_left_y;
    *rightX = g_analog_right_x;
    *rightY = g_analog_right_y;
}

void HandleInput(std::istringstream& arguments) {
    std::string maskText;
    if (!(arguments >> maskText)) {
        HarnessRepl::PrintErr("input: usage: input <mask>");
        return;
    }
    const auto mask = HarnessRepl::ParseNum(maskText);
    if (!mask) {
        HarnessRepl::PrintErr("input: bad mask");
        return;
    }
    SetInputMask(static_cast<uint32_t>(*mask));
    std::printf("ok\n");
}

} // namespace HarnessFrontend
