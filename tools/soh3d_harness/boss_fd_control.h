#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_CONTROL_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_CONTROL_H

#include <sstream>
#include <string_view>

#include "boss_fd2_mane_root_control.h"

namespace HarnessBossFdControl {

bool HandleForce(std::string_view subcommand, std::istringstream& arguments);
HarnessBossFd2ManeRootControl::Snapshot ManeRootControlSnapshot();
bool ManeRootControlAcceptsCurrent(const HarnessBossFd2ManeRootControl::Roots& oracle,
                                   const HarnessBossFd2ManeRootControl::Roots& soh);

} // namespace HarnessBossFdControl

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_CONTROL_H
