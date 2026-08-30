#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_FRAMEBUFFER_SNAPSHOT_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_FRAMEBUFFER_SNAPSHOT_H

#include <sstream>
#include <string>

namespace HarnessCapture {

bool WriteAzahar_Ppm(const std::string& path);
bool WriteSoh_Ppm(const std::string& path);
void HandleSnapshot(std::istringstream& arguments);
void HandleSohSnapshot(std::istringstream& arguments);

} // namespace HarnessCapture

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_FRAMEBUFFER_SNAPSHOT_H
