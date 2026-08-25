#pragma once

#include <sstream>
#include <string>

namespace HarnessSohPlayerControl {

bool HandleCommand(const std::string& command, std::istringstream& arguments);

} // namespace HarnessSohPlayerControl
