#pragma once

#include <cstdint>

namespace HarnessOracle {

class FirstDivReporter;

void CompareFirstDivActors(std::uint32_t oraclePlayState, FirstDivReporter& reporter);

} // namespace HarnessOracle
