#pragma once

#include <filesystem>

namespace Zelda3D::Platform {

bool EnsureRomSetup(const std::filesystem::path& dataRoot);

} // namespace Zelda3D::Platform
