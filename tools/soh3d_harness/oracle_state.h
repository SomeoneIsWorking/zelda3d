#pragma once

#include <cstdint>
#include <optional>
namespace HarnessOracle {

std::optional<uint32_t> CurrentPlayState();
std::optional<uint32_t> GameplayPlayState();
bool TitleActive();

} // namespace HarnessOracle
