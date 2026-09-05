#include "platform/rom_validation.h"

#include "asset/ctr_rom.h"
#include "extractor/n64_rom_validation.h"

namespace Zelda3D::Platform {
bool ValidateRomSelection(RomKind kind, const std::filesystem::path& path, std::string& error) {
    if (IdentifyRomFile(path) != kind) {
        error = "The file does not identify the requested game.";
        return false;
    }
    if (kind == RomKind::OotN64 || kind == RomKind::MmN64) {
        return Extractor::ValidateN64RomFile(kind, path, error);
    }
    CtrRom rom(path);
    if (!rom.ok() || !rom.validateContent()) {
        error = rom.error();
        return false;
    }
    return true;
}
} // namespace Zelda3D::Platform
