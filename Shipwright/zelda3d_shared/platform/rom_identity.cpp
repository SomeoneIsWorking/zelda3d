#include "platform/rom_identity.h"
#include "extractor/n64_rom_validation.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <string>

namespace Zelda3D::Platform {
namespace {

constexpr std::array<RomSpec, 4> kRomSpecs = { {
    { RomKind::OotN64, "oot-n64", "Ocarina of Time (N64)", "ZELDA3D_OOT_ROM", "oot.z64" },
    { RomKind::Oot3ds, "oot-3ds", "Ocarina of Time 3D (decrypted .3ds)", "ZELDA3D_OOT3D_ROM", "oot3d.3ds" },
    { RomKind::MmN64, "mm-n64", "Majora's Mask (N64)", "ZELDA3D_MM_ROM", "mm.z64" },
    { RomKind::Mm3ds, "mm-3ds", "Majora's Mask 3D (decrypted .3ds)", "ZELDA3D_MM3D_ROM", "mm3d.3ds" },
} };

uint32_t ReadLittleEndian32(std::span<const uint8_t> bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) | (static_cast<uint32_t>(bytes[3]) << 24U);
}

std::array<uint8_t, 0x40> NormalizeN64Header(const std::array<uint8_t, 0x40>& source) {
    std::array<uint8_t, 0x40> header = source;
    if (!Zelda3D::Extractor::NormalizeN64Rom(header)) {
        header.fill(0);
    }
    return header;
}

std::optional<RomKind> IdentifyN64(uint64_t size, const ReadAt& readAt) {
    if (size < 0x40) {
        return std::nullopt;
    }
    std::array<uint8_t, 0x40> source{};
    if (!readAt(0, source)) {
        return std::nullopt;
    }
    const auto header = NormalizeN64Header(source);
    if (std::all_of(header.begin(), header.end(), [](uint8_t byte) { return byte == 0; })) {
        return std::nullopt;
    }

    std::string title(reinterpret_cast<const char*>(header.data() + 0x20), 0x14);
    std::transform(title.begin(), title.end(), title.begin(),
                   [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
    const std::string gameCode(reinterpret_cast<const char*>(header.data() + 0x3B), 3);
    if (gameCode == "CZL" || title.find("LEGEND OF ZELDA") != std::string::npos) {
        return RomKind::OotN64;
    }
    if (gameCode == "NZS" || title.find("MAJORA") != std::string::npos) {
        return RomKind::MmN64;
    }
    return std::nullopt;
}

std::optional<RomKind> Identify3ds(uint64_t size, const ReadAt& readAt) {
    if (size < 0x400) {
        return std::nullopt;
    }
    std::array<uint8_t, 0x200> ncsd{};
    if (!readAt(0, ncsd) || !std::equal(ncsd.begin() + 0x100, ncsd.begin() + 0x104, "NCSD")) {
        return std::nullopt;
    }
    const uint64_t partitionOffset =
        static_cast<uint64_t>(ReadLittleEndian32(std::span<const uint8_t>(ncsd).subspan(0x120, 4))) * 0x200ULL;
    if (partitionOffset > size || size - partitionOffset < 0x200) {
        return std::nullopt;
    }
    std::array<uint8_t, 0x200> ncch{};
    if (!readAt(partitionOffset, ncch) || !std::equal(ncch.begin() + 0x100, ncch.begin() + 0x104, "NCCH")) {
        return std::nullopt;
    }
    const std::string productCode(reinterpret_cast<const char*>(ncch.data() + 0x150), 0x10);
    if (productCode.starts_with("CTR-P-AQE")) {
        return RomKind::Oot3ds;
    }
    if (productCode.starts_with("CTR-P-AJR")) {
        return RomKind::Mm3ds;
    }
    return std::nullopt;
}

} // namespace

std::span<const RomSpec> RomSpecs() {
    return kRomSpecs;
}

const RomSpec& GetRomSpec(RomKind kind) {
    const auto match =
        std::find_if(kRomSpecs.begin(), kRomSpecs.end(), [kind](const RomSpec& spec) { return spec.kind == kind; });
    if (match == kRomSpecs.end()) {
        throw std::invalid_argument("unknown Zelda3D ROM kind");
    }
    return *match;
}

std::optional<RomKind> IdentifyRom(uint64_t size, const ReadAt& readAt) {
    if (const auto n64 = IdentifyN64(size, readAt); n64.has_value()) {
        return n64;
    }
    return Identify3ds(size, readAt);
}

std::optional<RomKind> IdentifyRomFile(const std::filesystem::path& path) {
    std::error_code error;
    const uint64_t size = std::filesystem::file_size(path, error);
    if (error) {
        return std::nullopt;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    return IdentifyRom(size, [&stream, size](uint64_t offset, std::span<uint8_t> destination) {
        if (offset > size || destination.size() > size - offset) {
            return false;
        }
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(offset));
        stream.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));
        return stream.good();
    });
}

} // namespace Zelda3D::Platform
