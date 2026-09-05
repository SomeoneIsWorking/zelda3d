#pragma once

#include "platform/rom_install.h"

#include <string>
#include <vector>

namespace Zelda3D::Platform {

std::vector<std::string> RomArguments(int argc, char* argv[], RomKind fallbackKind,
                                      const RomSelectionStore& selectionStore);

} // namespace Zelda3D::Platform
