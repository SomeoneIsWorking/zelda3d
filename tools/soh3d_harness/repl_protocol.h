#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace HarnessRepl {

std::optional<uint64_t> ParseNum(const std::string& value);
void PrintErr(const char* message);

} // namespace HarnessRepl
