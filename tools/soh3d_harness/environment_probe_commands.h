#pragma once

#include <sstream>
#include <string>

namespace HarnessEnvironmentProbe {

bool HandleCommand(const std::string& command, std::istringstream& arguments);

} // namespace HarnessEnvironmentProbe
