#pragma once

#include <cstdint>

namespace Fast {

constexpr uint32_t EncodeColorCombiner(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a & 0xF) | ((b & 0xF) << 4) | ((c & 0x1F) << 8) | ((d & 7) << 13);
}

constexpr uint32_t EncodeAlphaCombiner(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a & 7) | ((b & 7) << 3) | ((c & 7) << 6) | ((d & 7) << 9);
}

constexpr int16_t SignExtend9(uint32_t value) {
    return static_cast<int16_t>((value & 0x100) != 0 ? static_cast<int32_t>(value | 0xFFFFFE00U)
                                                     : static_cast<int32_t>(value));
}

} // namespace Fast
