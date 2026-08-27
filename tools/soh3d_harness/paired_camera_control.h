#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_PAIRED_CAMERA_CONTROL_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_PAIRED_CAMERA_CONTROL_H

#include <array>
#include <cstdint>
#include <sstream>
#include <string_view>

namespace HarnessPairedCameraControl {

bool Apply(const std::array<float, 3>& oracleEye, const std::array<float, 3>& oracleAt,
           const std::array<float, 3>& sohEye, const std::array<float, 3>& sohAt, float fov);
bool HandleForce(std::string_view subcommand, std::istringstream& arguments);
void OverrideOracleWrite(uint32_t address, uint32_t size);

} // namespace HarnessPairedCameraControl

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_PAIRED_CAMERA_CONTROL_H
