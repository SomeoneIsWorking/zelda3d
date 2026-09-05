#include "rom_auto_extraction.h"

#include "app_identity.h"
#include "extractor/Extract.h"

#include <ship/Context.h>
#include <spdlog/spdlog.h>

#include "platform/rom_install.h"

#include <atomic>
#include <filesystem>
#include <string>

namespace {

#define ZELDA3D_BOOT(...)                           \
    do {                                            \
        SPDLOG_INFO("[zelda3d boot] " __VA_ARGS__); \
        if (auto _lg = spdlog::default_logger())    \
            _lg->flush();                           \
    } while (0)

bool VanillaArchiveExists() {
    return std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs("oot.o2r", kSohAppShortName)) ||
           std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs("oot-mq.o2r", kSohAppShortName));
}

} // namespace

bool Zelda3D_AutoExtractVanillaArchive() {
    const std::string installPath = Ship::Context::GetAppBundlePath();
    const Zelda3D::Platform::RomSelectionStore selectionStore(Ship::Context::GetAppDirectoryPath("zelda3d"));
    const auto selectedRom = selectionStore.ConfiguredSelection(Zelda3D::Platform::RomKind::OotN64);
    if (!selectedRom.has_value()) {
        ZELDA3D_BOOT("AutoExtract: no configured Ocarina of Time N64 ROM; launcher setup did not complete");
        return false;
    }

    Extractor extractor;
    ZELDA3D_BOOT("AutoExtract: validating launcher-selected ROM '{}'", selectedRom->string());
    if (!extractor.RunFileStandalone(selectedRom->string())) {
        ZELDA3D_BOOT("AutoExtract: launcher-selected ROM is no longer a supported OoT N64 ROM");
        return false;
    }

    const std::string dataPath = Ship::Context::GetAppDirectoryPath(kSohAppShortName);
    std::filesystem::create_directories(dataPath);
    std::atomic<size_t> extracted = 0;
    std::atomic<size_t> total = 0;
    ZELDA3D_BOOT("AutoExtract: extracting via ZAPD -> '{}' (this can take a bit)", dataPath);
    extractor.CallZapd(installPath, dataPath, &extracted, &total);
    const bool exists = VanillaArchiveExists();
    ZELDA3D_BOOT("AutoExtract: ZAPD finished (isMQ={}), oot.o2r present={}", extractor.IsMasterQuest(), exists);
    return exists;
}
