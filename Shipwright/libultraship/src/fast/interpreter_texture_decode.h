#pragma once

#include <cstdint>

namespace Fast {

constexpr uint8_t Scale5To8(uint32_t value) {
    return static_cast<uint8_t>((value * 0xFF) / 0x1F);
}

constexpr uint8_t Scale4To8(uint32_t value) {
    return static_cast<uint8_t>(value * 0x11);
}

constexpr uint8_t Scale3To8(uint32_t value) {
    return static_cast<uint8_t>(value * 0x24);
}

} // namespace Fast
