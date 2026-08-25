#pragma once

#include <sstream>
#include <string>

namespace HarnessPlayerProbe {

bool HandleCommand(const std::string& command, std::istringstream& arguments);

} // namespace HarnessPlayerProbe
