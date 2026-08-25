#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_TITLE_PROBE_COMMANDS_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_TITLE_PROBE_COMMANDS_H

#include <sstream>
#include <string>

namespace HarnessTitleProbe {
bool HandleCommand(const std::string& command, std::istringstream& arguments);
}

#endif
