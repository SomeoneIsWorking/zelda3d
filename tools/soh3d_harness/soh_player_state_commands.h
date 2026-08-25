#pragma once

#include <sstream>
#include <string>

namespace HarnessSohPlayerState {

bool HandleCommand(const std::string& command, std::istringstream& arguments);

} // namespace HarnessSohPlayerState
