#pragma once

#include <sstream>
#include <string>

namespace HarnessWatchCommands {

bool HandleCommand(const std::string& command, std::istringstream& arguments);

} // namespace HarnessWatchCommands
