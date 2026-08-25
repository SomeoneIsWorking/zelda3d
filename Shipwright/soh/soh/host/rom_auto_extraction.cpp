#include "rom_auto_extraction.h"

#include "app_identity.h"
#include "extractor/Extract.h"

#include <ship/Context.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace {

#define ZELDA3D_BOOT(...)                           \
    do {                                            \
        SPDLOG_INFO("[zelda3d boot] " __VA_ARGS__); \
        if (auto _lg = spdlog::default_logger())    \
            _lg->flush();                           \
    } while (0)

std::vector<std::filesystem::path> CandidateDirectories(const std::filesystem::path& installPath) {
    std::error_code error;
    std::vector<std::filesystem::path> directories = { std::filesystem::current_path(error) };
    for (std::filesystem::path path = installPath;
         !path.empty() && path != path.root_path() && directories.size() < 8; path = path.parent_path()) {
        directories.push_back(path);
    }
    return directories;
}

std::vector<std::string> FindCandidateRoms(const std::vector<std::filesystem::path>& directories) {
    std::error_code error;
    std::vector<std::string> roms;
    for (const auto& directory : directories) {
        if (directory.empty() || !std::filesystem::is_directory(directory, error)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
            if (!entry.is_regular_file(error)) {
                continue;
            }
            const std::string extension = entry.path().extension().string();
            if (extension != ".z64" && extension != ".n64" && extension != ".v64") {
                continue;
            }
            const std::string absolutePath = std::filesystem::absolute(entry.path(), error).string();
            if (std::find(roms.begin(), roms.end(), absolutePath) == roms.end()) {
                roms.push_back(absolutePath);
            }
        }
    }
    return roms;
}

bool VanillaArchiveExists() {
    return std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs("oot.o2r", kSohAppShortName)) ||
           std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs("oot-mq.o2r", kSohAppShortName));
}

} // namespace

bool Zelda3D_AutoExtractVanillaArchive() {
    const std::string installPath = Ship::Context::GetAppBundlePath();
    const std::vector<std::filesystem::path> directories = CandidateDirectories(installPath);
    const std::vector<std::string> roms = FindCandidateRoms(directories);

    ZELDA3D_BOOT("AutoExtract: scanned {} dir(s), found {} candidate ROM(s)", directories.size(), roms.size());
    for (const std::string& rom : roms) {
        Extractor extractor;
        ZELDA3D_BOOT("AutoExtract: validating ROM '{}'", rom);
        if (!extractor.RunFileStandalone(rom)) {
            ZELDA3D_BOOT("AutoExtract: '{}' is not a supported OoT N64 ROM, skipping", rom);
            continue;
        }

        std::atomic<size_t> extracted = 0;
        std::atomic<size_t> total = 0;
        ZELDA3D_BOOT("AutoExtract: extracting '{}' via ZAPD -> '{}' (this can take a bit)", rom, installPath);
        extractor.CallZapd(installPath, installPath, &extracted, &total);
        const bool exists = VanillaArchiveExists();
        ZELDA3D_BOOT("AutoExtract: ZAPD finished (isMQ={}), oot.o2r present={}", extractor.IsMasterQuest(), exists);
        if (exists) {
            return true;
        }
    }
    return false;
}
