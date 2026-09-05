#include "platform/rom_paths.h"

namespace Zelda3D::Platform {
std::string RomPathToUtf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return { text.begin(), text.end() };
}
std::filesystem::path RomPathFromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
} // namespace Zelda3D::Platform
