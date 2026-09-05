#include "platform/rom_archive.h"

#include "platform/rom_validation.h"
#include <lucent/zip.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <system_error>

namespace Zelda3D::Platform {
namespace {
// Decrypted 3DS cartridges are at most 2 GiB for the supported titles. One nested
// ZIP shares a 4 GiB compressed/expanded budget across both inspected levels.
constexpr lucent::zip::ExtractionLimits kLimits{
    .max_archive_bytes = 4ULL * 1024 * 1024 * 1024,
    .max_extracted_bytes = 4ULL * 1024 * 1024 * 1024,
    .max_entry_bytes = 2ULL * 1024 * 1024 * 1024,
    .max_entries = 4096,
};

class StagingDirectory {
  public:
    explicit StagingDirectory(std::filesystem::path path) : mPath(std::move(path)) {}
    ~StagingDirectory() {
        std::error_code error;
        std::filesystem::remove_all(mPath, error);
    }
    const std::filesystem::path& Path() const {
        return mPath;
    }

  private:
    std::filesystem::path mPath;
};
} // namespace

RomImportResult ImportRomArchive(RomKind kind, const std::filesystem::path& source,
                                 const std::filesystem::path& dataRoot) {
    const auto generation = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto& spec = GetRomSpec(kind);
    const auto stem = std::string(spec.id) + "-" + std::to_string(generation);
    const auto destination =
        dataRoot / "roms" / (stem + std::filesystem::path(spec.cachedFileName).extension().string());
    const auto stagingPath = dataRoot / "roms" / (stem + ".staging");
    std::error_code filesystemError;
    std::filesystem::create_directories(destination.parent_path(), filesystemError);
    if (filesystemError) {
        return { false, {}, "Cannot create ROM storage: " + filesystemError.message() };
    }
    // Never own or clean up a path that another operation already owns.
    if (std::filesystem::exists(stagingPath) || std::filesystem::exists(destination)) {
        return { false, {}, "ROM import generation already exists." };
    }
    StagingDirectory staging(stagingPath);
    std::filesystem::path matched;
    std::string error;
    const auto matcher = [kind](std::string_view, std::span<const uint8_t> bytes) {
        return IdentifyRom(bytes.size(), [bytes](uint64_t offset, std::span<uint8_t> target) {
                   if (offset > bytes.size() || target.size() > bytes.size() - offset)
                       return false;
                   std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), target.size(), target.begin());
                   return true;
               }) == kind;
    };
    if (!lucent::zip::extract_unique_install(source, staging.Path(), matcher, matched, error, kLimits) ||
        !ValidateRomSelection(kind, matched, error)) {
        return { false, {}, error };
    }
    std::filesystem::rename(matched, destination, filesystemError);
    if (filesystemError) {
        return { false, {}, "Cannot publish validated ROM: " + filesystemError.message() };
    }
    return { true, destination, {} };
}
} // namespace Zelda3D::Platform
