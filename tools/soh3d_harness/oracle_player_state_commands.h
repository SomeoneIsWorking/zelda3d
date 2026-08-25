#pragma once

#include <sstream>
#include <string>

namespace HarnessOraclePlayerState {

bool HandleCommand(const std::string& command, std::istringstream& arguments);

} // namespace HarnessOraclePlayerState
