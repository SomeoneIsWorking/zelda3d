#include "oracle_lighting_compare.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "soh_lighting_state.h"
#include "soh_play_state.h"

namespace HarnessOracle {

void CompareLightingImpl() {
    auto& memory = Core::System::GetInstance().Memory();
    constexpr uint32_t kFallbackPlayStateAddress = 0x0871E840;
    const auto playStatePointer = memory.Read32OrNullopt(OracleLayout::kPlayStateAddress);
    const uint32_t playState =
        playStatePointer && *playStatePointer != 0 ? *playStatePointer : kFallbackPlayStateAddress;
    const uint32_t environmentContext = playState + 0x3135;
    const auto slots = memory.Read32OrNullopt(environmentContext + 0xA4);
    const auto mode = memory.Read32OrNullopt(environmentContext + 0xCC);
    const auto weightBits = memory.Read32OrNullopt(environmentContext + 0xC8);
    const auto byteFrom = [](std::optional<uint32_t> word, uint32_t byteIndex) -> unsigned {
        return word ? static_cast<unsigned>((*word >> (byteIndex * 8)) & 0xFF) : 0xFFU;
    };
    float weight = 0.0F;
    if (weightBits) {
        std::memcpy(&weight, &*weightBits, sizeof(float));
    }
    std::printf("  3ds: envCtx@0x%08x  slot=%u  prevSlot=%u  lerpWeight=%.3f  mode=0x%02x\n", environmentContext,
                byteFrom(slots, 1), byteFrom(slots, 2), weightBits ? weight : 0.0F, byteFrom(mode, 0));

    if (!SohState_HasPlayState()) {
        std::printf("  soh: n/a (no playstate)\n");
        return;
    }
    unsigned char ambient[3] = {};
    signed char light1Direction[3] = {};
    unsigned char light1Color[3] = {};
    signed char light2Direction[3] = {};
    unsigned char light2Color[3] = {};
    unsigned char fog[3] = {};
    short fogNear = 0;
    short fogFar = 0;
    unsigned char lightContextAmbient[3] = {};
    unsigned char lightContextFog[3] = {};
    short lightContextFogNear = 0;
    short lightContextFogFar = 0;
    unsigned char currentSlot = 0xFF;
    unsigned char previousSlot = 0xFF;
    float interpolationWeight = 0.0F;
    if (!SohState_Lighting(ambient, light1Direction, light1Color, light2Direction, light2Color, fog, &fogNear, &fogFar,
                           lightContextAmbient, lightContextFog, &lightContextFogNear, &lightContextFogFar,
                           &currentSlot, &previousSlot, &interpolationWeight)) {
        std::printf("  soh: n/a (SohState_Lighting failed)\n");
        return;
    }
    std::printf("  soh: slot=%u  prevSlot=%u  lerpWeight=%.3f\n"
                "       envLightSettings ambient=(%u,%u,%u) fog=(%u,%u,%u) fogNear=%d fogFar=%d\n"
                "       light1 dir=(%d,%d,%d) color=(%u,%u,%u)\n"
                "       light2 dir=(%d,%d,%d) color=(%u,%u,%u)\n",
                static_cast<unsigned>(currentSlot), static_cast<unsigned>(previousSlot), interpolationWeight,
                ambient[0], ambient[1], ambient[2], fog[0], fog[1], fog[2], fogNear, fogFar, light1Direction[0],
                light1Direction[1], light1Direction[2], light1Color[0], light1Color[1], light1Color[2],
                light2Direction[0], light2Direction[1], light2Direction[2], light2Color[0], light2Color[1],
                light2Color[2]);
    std::printf("  soh: lightCtx  ambient=(%u,%u,%u) fog=(%u,%u,%u) fogNear=%d fogFar=%d\n", lightContextAmbient[0],
                lightContextAmbient[1], lightContextAmbient[2], lightContextFog[0], lightContextFog[1],
                lightContextFog[2], lightContextFogNear, lightContextFogFar);
}

} // namespace HarnessOracle
