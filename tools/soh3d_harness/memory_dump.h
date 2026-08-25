#pragma once

#include <sstream>

namespace HarnessMemoryDump {
void HandlePhysical(std::istringstream& arguments);
void HandleVirtual(std::istringstream& arguments);
} // namespace HarnessMemoryDump
