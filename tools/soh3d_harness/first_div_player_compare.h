#pragma once

#include "first_div_policy.h"

#include <cstdint>

namespace HarnessOracle {

class FirstDivReporter;

struct GameplayPlayerComparison {
    DivDecision positionDecision = kUnclassified;
    std::uint32_t cameraEyeWatchAddress = 0;
    std::uint32_t deltaAWatchAddress = 0;
};

GameplayPlayerComparison CompareFirstDivPlayer(std::uint32_t oraclePlayState, FirstDivReporter& reporter);

} // namespace HarnessOracle
