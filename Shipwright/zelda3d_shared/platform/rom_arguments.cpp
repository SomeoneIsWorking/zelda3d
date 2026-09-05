#include "platform/rom_arguments.h"

namespace Zelda3D::Platform {

std::vector<std::string> RomArguments(int argc, char* argv[], RomKind fallbackKind,
                                      const RomSelectionStore& selectionStore) {
    std::vector<std::string> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    if (arguments.empty()) {
        if (const auto selection = selectionStore.ConfiguredSelection(fallbackKind); selection.has_value()) {
            arguments.emplace_back(selection->string());
        }
    }
    return arguments;
}

} // namespace Zelda3D::Platform
