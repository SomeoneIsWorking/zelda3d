#pragma once

#include <cstdint>
#include <optional>

namespace HarnessOracle {

struct FirstDivEngineState {
    bool oracleAtTitle = false;
    bool sohAtTitle = false;
    bool oracleInPlay = false;
    bool sohInPlay = false;
    std::optional<std::uint32_t> oraclePlayState;
    unsigned oracleScene = 0xFFFF;
    unsigned sohScene = 0xFFFF;

    bool BothInSameGameplayScene() const;
};

FirstDivEngineState CaptureFirstDivEngineState();

} // namespace HarnessOracle
