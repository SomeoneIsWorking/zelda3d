#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Zelda3D::Platform {
// Serialized paths use UTF-8; filesystem and Windows environment operations use native paths.
std::string RomPathToUtf8(const std::filesystem::path& path);
std::filesystem::path RomPathFromUtf8(std::string_view text);
} // namespace Zelda3D::Platform
