#pragma once

#include <sstream>
#include <string>

namespace HarnessFrontend {

void HandleRun(std::istringstream& arguments);
bool HandleTimingCommand(const std::string& command, std::istringstream& arguments);

} // namespace HarnessFrontend
