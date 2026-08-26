#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_PAIRED_CAMERA_CONTROL_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_PAIRED_CAMERA_CONTROL_H

#include <cstdint>
#include <sstream>
#include <string_view>

namespace HarnessPairedCameraControl {

bool HandleForce(std::string_view subcommand, std::istringstream& arguments);
void OverrideOracleWrite(uint32_t address, uint32_t size);

} // namespace HarnessPairedCameraControl

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_PAIRED_CAMERA_CONTROL_H
