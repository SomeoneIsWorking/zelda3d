#pragma once

#include "platform/rom_identity.h"
#include <filesystem>
#include <string>

namespace Zelda3D::Platform {
// Acceptance is separate from cheap discovery: N64 extraction revision/whole-image CRC,
// or readable decrypted CTR asset filesystem with complete content-hash verification.
bool ValidateRomSelection(RomKind kind, const std::filesystem::path& path, std::string& error);
} // namespace Zelda3D::Platform
