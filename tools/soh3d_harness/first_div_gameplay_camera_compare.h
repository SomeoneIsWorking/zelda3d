#pragma once

#include <cstdint>

namespace HarnessOracle {

class FirstDivReporter;
struct GameplayPlayerComparison;

void CompareGameplayCameraFirstDiv(std::uint32_t oraclePlayState, const GameplayPlayerComparison& playerComparison,
                                   FirstDivReporter& reporter);

} // namespace HarnessOracle
