#pragma once

#include <sstream>
#include <string>

namespace HarnessFrontendInput {

bool HandleCommand(const std::string& command, std::istringstream& arguments);

} // namespace HarnessFrontendInput
