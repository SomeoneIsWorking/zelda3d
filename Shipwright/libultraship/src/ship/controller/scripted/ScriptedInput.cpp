#include "ship/controller/scripted/ScriptedInput.h"

#include "libultraship/libultra/controller.h" // OSContPad

#include <atomic>
#include <cstdlib>

// Lock-free synthetic pad state. The game thread reads it via MixInto() every frame while a
// FIFO-poller thread may update it; atomics are sufficient because each field is independent
// and a frame tearing one field across a write is harmless (the next frame corrects it).
namespace {
std::atomic<bool> gEnabled{ false };
std::atomic<uint16_t> gButtons{ 0 };
std::atomic<int8_t> gStickX{ 0 };
std::atomic<int8_t> gStickY{ 0 };

// Larger-magnitude axis wins, so scripted motion drives a headless run (physical == 0) yet a
// human pushing harder still overrides on a live shared pad.
inline int8_t mergeAxis(int8_t physical, int8_t scripted) {
    return (std::abs((int)scripted) > std::abs((int)physical)) ? scripted : physical;
}
} // namespace

extern "C" {

void Ship_ScriptedInput_SetEnabled(int enabled) {
    gEnabled.store(enabled != 0, std::memory_order_relaxed);
}

int Ship_ScriptedInput_IsEnabled(void) {
    return gEnabled.load(std::memory_order_relaxed) ? 1 : 0;
}

void Ship_ScriptedInput_SetButtons(uint16_t buttonMask) {
    gButtons.store(buttonMask, std::memory_order_relaxed);
}

uint16_t Ship_ScriptedInput_GetButtons(void) {
    return gButtons.load(std::memory_order_relaxed);
}

void Ship_ScriptedInput_SetStick(int8_t x, int8_t y) {
    gStickX.store(x, std::memory_order_relaxed);
    gStickY.store(y, std::memory_order_relaxed);
}

void Ship_ScriptedInput_MixInto(void* osContPad) {
    if (osContPad == nullptr || !gEnabled.load(std::memory_order_relaxed)) {
        return;
    }
    OSContPad* pad = static_cast<OSContPad*>(osContPad);
    pad->button |= gButtons.load(std::memory_order_relaxed);
    pad->stick_x = mergeAxis(pad->stick_x, gStickX.load(std::memory_order_relaxed));
    pad->stick_y = mergeAxis(pad->stick_y, gStickY.load(std::memory_order_relaxed));
}

} // extern "C"
