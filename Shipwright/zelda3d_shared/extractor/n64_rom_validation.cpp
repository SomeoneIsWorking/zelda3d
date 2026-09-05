#include "extractor/n64_rom_validation.h"
#include <algorithm>
#include <array>
#include <fstream>

extern "C" uint32_t CRC32C(unsigned char* data, size_t dataSize);

namespace Zelda3D::Extractor {
bool IsSupportedN64RomSize(uint64_t size) {
    constexpr uint64_t megabyte = 1024 * 1024;
    return size == 32 * megabyte || size == 54 * megabyte || size == 64 * megabyte;
}

bool NormalizeN64Rom(std::span<unsigned char> bytes) {
    if (bytes.size() < 4 || bytes.size() % 4 != 0) {
        return false;
    }
    const std::array<unsigned char, 4> magic = { bytes[0], bytes[1], bytes[2], bytes[3] };
    if (magic == std::array<unsigned char, 4>{ 0x80, 0x37, 0x12, 0x40 }) {
        return true;
    }
    size_t stride = 0;
    if (magic == std::array<unsigned char, 4>{ 0x37, 0x80, 0x40, 0x12 }) {
        stride = 2;
    } else if (magic == std::array<unsigned char, 4>{ 0x40, 0x12, 0x37, 0x80 }) {
        stride = 4;
    } else {
        return false;
    }
    for (size_t offset = 0; offset < bytes.size(); offset += stride) {
        std::reverse(bytes.begin() + offset, bytes.begin() + offset + stride);
    }
    return true;
}

uint32_t N64HeaderCrc(std::span<const unsigned char> bytes) {
    if (bytes.size() < 0x14) {
        return 0;
    }
    return (static_cast<uint32_t>(bytes[0x10]) << 24U) | (static_cast<uint32_t>(bytes[0x11]) << 16U) |
           (static_cast<uint32_t>(bytes[0x12]) << 8U) | static_cast<uint32_t>(bytes[0x13]);
}

bool ValidateN64Rom(std::span<unsigned char> bytes, const RomVersionTable& table, std::string& error) {
    error.clear();
    if (!IsSupportedN64RomSize(bytes.size())) {
        error = "N64 ROM must contain exactly 32, 54, or 64 MiB; received " + std::to_string(bytes.size()) + " bytes";
        return false;
    }
    if (!NormalizeN64Rom(bytes)) {
        error = "N64 ROM byte-order signature is invalid; select an uncompressed ROM image";
        return false;
    }
    const uint32_t headerCrc = N64HeaderCrc(bytes);
    const RomVersion* version = nullptr;
    for (size_t index = 0; index < table.versionCount; ++index) {
        if (table.versions[index].headerCrc == headerCrc) {
            version = &table.versions[index];
            break;
        }
    }
    if (version == nullptr || version->zapdVerStr == nullptr) {
        error = version == nullptr ? "N64 ROM version is not supported for this title"
                                   : std::string("N64 ROM version cannot be extracted: ") + version->name;
        return false;
    }
    for (size_t index = 0; index < table.headerPatchCount; ++index) {
        const auto& patch = table.headerPatches[index];
        if (patch.headerCrc == headerCrc) {
            if (patch.offset >= bytes.size()) {
                error = "N64 ROM validation metadata contains an invalid header patch offset";
                return false;
            }
            bytes[patch.offset] = patch.value;
        }
    }
    const uint32_t crc = CRC32C(bytes.data(), bytes.size());
    for (size_t index = 0; index < table.goodCrcCount; ++index) {
        if (table.goodCrcs[index] == crc) {
            return true;
        }
    }
    error = "N64 ROM whole-file CRC32C does not match a supported dump";
    return false;
}

bool LoadValidatedN64Rom(const std::filesystem::path& path, const RomVersionTable& table,
                         std::vector<unsigned char>& bytes, std::string& error) {
    std::error_code filesystemError;
    const auto size = std::filesystem::file_size(path, filesystemError);
    if (filesystemError) {
        error = "Cannot inspect ROM file: " + filesystemError.message();
        return false;
    }
    if (!IsSupportedN64RomSize(size)) {
        error = "N64 ROM must contain exactly 32, 54, or 64 MiB; received " + std::to_string(size) + " bytes";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Cannot open ROM file";
        return false;
    }
    std::vector<unsigned char> candidate(size);
    stream.read(reinterpret_cast<char*>(candidate.data()), static_cast<std::streamsize>(candidate.size()));
    if (!stream || stream.peek() != std::char_traits<char>::eof()) {
        error = "Cannot read the complete ROM file, or its size changed while reading";
        return false;
    }
    if (!ValidateN64Rom(candidate, table, error)) {
        return false;
    }
    bytes = std::move(candidate);
    return true;
}

bool ValidateN64RomFile(Platform::RomKind kind, const std::filesystem::path& path, std::string& error) {
    const RomVersionTable* table = nullptr;
    switch (kind) {
        case Platform::RomKind::OotN64:
            table = &OotRomVersionTable();
            break;
        case Platform::RomKind::MmN64:
            table = &MmRomVersionTable();
            break;
        default:
            error = "N64 validation requires an N64 ROM kind";
            return false;
    }
    std::vector<unsigned char> bytes;
    return LoadValidatedN64Rom(path, *table, bytes, error);
}
} // namespace Zelda3D::Extractor
