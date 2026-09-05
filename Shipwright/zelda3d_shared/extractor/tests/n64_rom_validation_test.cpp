#include "extractor/n64_rom_validation.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

extern "C" uint32_t CRC32C(unsigned char* data, size_t dataSize);

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void Write(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    Require(!stream.fail(), "synthetic ROM file could not be written");
}
} // namespace

int main(int argc, char** argv) {
    using namespace Zelda3D::Extractor;
    Require(argc == 2, "test requires an isolated fixture directory");
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    std::vector<unsigned char> knownCrc = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    Require(CRC32C(knownCrc.data(), knownCrc.size()) == 0xE3069283, "CRC32C standard discriminator failed");
    std::vector<unsigned char> canonical(32 * 1024 * 1024);
    canonical[0] = 0x80;
    canonical[1] = 0x37;
    canonical[2] = 0x12;
    canonical[3] = 0x40;
    canonical[0x10] = 0x12;
    canonical[0x11] = 0x34;
    canonical[0x12] = 0x56;
    canonical[0x13] = 0x78;
    canonical.back() = 0xA5;
    const uint32_t crc = CRC32C(canonical.data(), canonical.size());
    RomVersion version{ 0x12345678, "synthetic", "synthetic", false };
    RomVersionTable table{ &version, 1, &crc, 1, nullptr, 0, "synthetic", nullptr, "", false };
    std::string error;
    for (size_t stride : { 1, 2, 4 }) {
        auto bytes = canonical;
        for (size_t offset = 0; offset < bytes.size(); offset += stride) {
            std::reverse(bytes.begin() + offset, bytes.begin() + offset + stride);
        }
        Require(ValidateN64Rom(bytes, table, error), "supported byte order rejected");
        Require(bytes == canonical, "normalization changed canonical bytes");
    }
    auto bytes = canonical;
    bytes.back() ^= 1;
    Require(!ValidateN64Rom(bytes, table, error), "corrupt body accepted despite whole CRC");
    bytes.resize(0x40);
    Require(!ValidateN64Rom(bytes, table, error), "header-only fixture accepted");
    bytes = canonical;
    version.zapdVerStr = nullptr;
    Require(!ValidateN64Rom(bytes, table, error), "unextractable version accepted");
    version.zapdVerStr = "synthetic";
    version.headerCrc ^= 1;
    Require(!ValidateN64Rom(bytes, table, error), "wrong-title version accepted");
    version.headerCrc ^= 1;
    bytes[0] = 'P';
    bytes[1] = 'K';
    Require(!ValidateN64Rom(bytes, table, error), "invalid ROM signature accepted");
    bytes = canonical;
    bytes[0x3E] = 'E';
    const RomHeaderPatch patch{ version.headerCrc, 0x3E, 0 };
    table.headerPatches = &patch;
    table.headerPatchCount = 1;
    Require(ValidateN64Rom(bytes, table, error), "supported header repair failed");
    Require(bytes == canonical, "header repair changed unrelated bytes");
    for (const auto* name : { "synthetic.bin", "synthetic.Z64" }) {
        const auto path = directory / name;
        Write(path, canonical);
        std::vector<unsigned char> loaded;
        Require(LoadValidatedN64Rom(path, table, loaded, error), "content-valid filename rejected");
        Require(loaded == canonical, "file validator did not read the whole image");
        Write(path, std::vector<unsigned char>(0x40));
        Require(!LoadValidatedN64Rom(path, table, loaded, error), "truncated file accepted");
        Require(loaded == canonical, "failed validation replaced prior bytes");
        std::filesystem::remove(path);
    }
    Require(!ValidateN64RomFile(Zelda3D::Platform::RomKind::Oot3ds, directory, error),
            "3DS kind accepted by N64 validator");
    Require(!ValidateN64RomFile(Zelda3D::Platform::RomKind::OotN64, directory / "missing", error),
            "missing file accepted");
    return 0;
}
