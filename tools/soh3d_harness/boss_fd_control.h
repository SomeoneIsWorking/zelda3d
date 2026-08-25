#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_CONTROL_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_CONTROL_H

#include <sstream>
#include <string_view>

namespace HarnessBossFdControl {

bool HandleForce(std::string_view subcommand, std::istringstream& arguments);

} // namespace HarnessBossFdControl

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_CONTROL_H
