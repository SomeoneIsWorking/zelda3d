#include "asset/ctr_rom.h"
#include "ctr_rom_fixture.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
void verify(const std::filesystem::path& path) {
    using CtrRomTest::Image;
    Image image;
    image.write(path);
    Zelda3D::CtrRom rom(path);
    require(rom.ok(), rom.error());
    require(rom.validateContent(), rom.error());
    const auto summary = rom.validationSummary();
    require(summary.integrityVerified && summary.imageBytes == image.bytes.size() && summary.directories == 1 &&
                summary.files == 1 && summary.fileBytes == 3 && summary.verifiedBlocks == 3 &&
                summary.verifiedBytes == 0x3200,
            "complete validation summary");
    require(rom.read("/a") == std::vector<uint8_t>({ 'a', 'b', 'c' }), "synthetic file content");
    Zelda3D::CtrFile forged{ "a", "/a", 0, 3 };
    require(rom.read(forged).empty(), "forged file range must be refused");

    Image nested("CTR-P-AQEE", true, u'\u00e9');
    nested.write(path);
    Zelda3D::CtrRom nestedRom(path);
    require(nestedRom.ok(), nestedRom.error());
    require(nestedRom.validateContent(), nestedRom.error());
    require(nestedRom.read("/d/\xC3\xA9") == std::vector<uint8_t>({ 'a', 'b', 'c' }), "nested Unicode file content");

    Image flagged;
    flagged.bytes[Image::Ncch + 0x18F] = 0;
    flagged.write(path);
    Zelda3D::CtrRom flaggedRom(path);
    require(flaggedRom.ok() && flaggedRom.validateContent(), "verified plaintext with retained encryption flag");

    const auto refuse = [&](const char* reason, const auto& mutate, bool structure = true) {
        Image bad;
        mutate(bad);
        bad.write(path);
        Zelda3D::CtrRom reader(path);
        if (structure)
            require(!reader.ok(), std::string(reason) + " passed structural validation");
        else {
            require(reader.ok(), std::string(reason) + " must reach integrity check: " + reader.error());
            require(!reader.validateContent(), std::string(reason) + " passed integrity validation");
            require(!reader.ok() && reader.get("/a") == nullptr, "failed integrity must invalidate reader");
        }
        require(!reader.error().empty(), std::string(reason) + " has no diagnostic");
    };
    refuse("header truncation", [](Image& i) { i.bytes.resize(1024); });
    refuse("partition overflow", [](Image& i) { i.word(0x120, UINT32_MAX); });
    refuse("partition truncation", [](Image& i) { i.bytes.pop_back(); });
    refuse("empty partition", [](Image& i) { i.word(0x124, 0); });
    refuse("NCCH extent", [](Image& i) { i.word(Image::Ncch + 0x104, UINT32_MAX); });
    refuse("encrypted image", [](Image& i) {
        i.bytes[Image::Ncch + 0x18F] = 0;
        std::fill(i.bytes.begin() + Image::Romfs, i.bytes.end(), 0xA5);
    });
    refuse("flagged corrupt plaintext", [](Image& i) {
        i.bytes[Image::Ncch + 0x18F] = 0;
        i.bytes[Image::Data] ^= 1;
    });
    refuse("media shift", [](Image& i) { i.bytes[Image::Ncch + 0x18E] = 255; });
    refuse("RomFS overflow", [](Image& i) { i.word(Image::Ncch + 0x1B0, UINT32_MAX); });
    refuse("IVFC shift", [](Image& i) { i.word(Image::Romfs + 0x4C, 64); });
    refuse("IVFC extent", [](Image& i) { i.wide(Image::Romfs + 0x44, UINT64_MAX); });
    refuse("IVFC coverage", [](Image& i) { i.wide(Image::Romfs + 0x14, 64); });
    refuse("metadata overlap", [](Image& i) { i.word(Image::L3 + 12, 0); });
    refuse("metadata budget", [](Image& i) { i.word(Image::L3 + 16, UINT32_MAX); });
    refuse("name truncation", [](Image& i) { i.word(Image::File + 28, 510); });
    refuse("odd UTF16", [](Image& i) { i.word(Image::File + 28, 1); });
    refuse("UTF16 surrogate", [](Image& i) { i.bytes[Image::File + 33] = 0xD8; });
    refuse("path separator", [](Image& i) { i.bytes[Image::File + 32] = '/'; });
    refuse("directory cycle", [](Image& i) { i.word(Image::Dir + 8, 0); });
    refuse("file cycle", [](Image& i) { i.word(Image::File + 4, 0); });
    refuse("hash cycle", [](Image& i) { i.word(Image::File + 24, 0); });
    refuse("hash interior offset", [](Image& i) { i.word(Image::L3 + 0x44, 4); });
    refuse("orphan file", [](Image& i) { i.word(Image::Dir + 12, UINT32_MAX); });
    refuse("wrong parent", [](Image& i) { i.word(Image::File, 24); });
    refuse("file overflow", [](Image& i) { i.wide(Image::File + 8, UINT64_MAX); });
    refuse("file size overflow", [](Image& i) { i.wide(Image::File + 16, UINT64_MAX); });
    refuse("superblock corruption", [](Image& i) { i.bytes[Image::Romfs + 0x60] ^= 1; }, false);
    refuse("level1 corruption", [](Image& i) { i.bytes[Image::Romfs + 0x2000] ^= 1; }, false);
    refuse("level2 corruption", [](Image& i) { i.bytes[Image::Romfs + 0x3000] ^= 1; }, false);
    refuse("file corruption", [](Image& i) { i.bytes[Image::Data] ^= 1; }, false);
    refuse("padding corruption", [](Image& i) { i.bytes[Image::Data + 20] ^= 1; }, false);
}
void verifyNativePath(const std::filesystem::path& path) {
    CtrRomTest::Image{}.write(path);
    Zelda3D::CtrRom image(path);
    require(image.ok(), image.error());
    require(image.validateContent(), image.error());
    require(image.read("/a") == std::vector<uint8_t>({ 'a', 'b', 'c' }), "non-ASCII host filename");
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: ctr_rom_test <scratch-directory>\n";
        return 2;
    }
    const auto directory = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(directory);
    const auto path = directory / "synthetic-ctr.3ds";
    const auto nativePath = directory / std::filesystem::path(u8"cartridge-\u00E9-\u65E5.3ds");
    try {
        verify(path);
        verifyNativePath(nativePath);
        std::filesystem::remove(path);
        std::filesystem::remove(nativePath);
        std::cout << "CTR reader: 4 valid hashed images and 31 negative cases passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::filesystem::remove(path);
        std::filesystem::remove(nativePath);
        return 1;
    }
}
