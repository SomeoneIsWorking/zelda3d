#pragma once

#include <cstdint>

namespace HarnessOracle {

class FirstDivReporter;

void CompareGameplayFirstDiv(std::uint32_t oraclePlayState, FirstDivReporter& reporter);

} // namespace HarnessOracle
