#pragma once

#include <sstream>
#include <string>

namespace HarnessRenderDebug {

bool HandleCommand(const std::string& command, std::istringstream& arguments);

} // namespace HarnessRenderDebug
