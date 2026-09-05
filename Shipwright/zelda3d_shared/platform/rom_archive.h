#pragma once

#include "platform/rom_install.h"

namespace Zelda3D::Platform {
// Lucent owns archive safety and discovery; this owner retains only a completely
// validated ROM in a unique managed generation and removes other staged entries.
RomImportResult ImportRomArchive(RomKind kind, const std::filesystem::path& source,
                                 const std::filesystem::path& dataRoot);
} // namespace Zelda3D::Platform
