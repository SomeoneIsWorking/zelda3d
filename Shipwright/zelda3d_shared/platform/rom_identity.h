#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace Zelda3D::Platform {

enum class RomKind {
    OotN64,
    Oot3ds,
    MmN64,
    Mm3ds,
};

struct RomSpec {
    RomKind kind;
    std::string_view id;
    std::string_view label;
    std::string_view environmentVariable;
    std::string_view cachedFileName;
};

using ReadAt = std::function<bool(uint64_t offset, std::span<uint8_t> destination)>;

std::span<const RomSpec> RomSpecs();
const RomSpec& GetRomSpec(RomKind kind);
std::optional<RomKind> IdentifyRom(uint64_t size, const ReadAt& readAt);
std::optional<RomKind> IdentifyRomFile(const std::filesystem::path& path);

} // namespace Zelda3D::Platform
