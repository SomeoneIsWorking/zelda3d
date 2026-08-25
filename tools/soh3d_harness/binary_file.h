#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_BINARY_FILE_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_BINARY_FILE_H

#include <cstdint>
#include <string>
#include <vector>

namespace HarnessBinaryFile {

std::vector<uint8_t> Read(const std::string& path);
bool Write(const std::string& path, const std::vector<uint8_t>& data);

} // namespace HarnessBinaryFile

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_BINARY_FILE_H
