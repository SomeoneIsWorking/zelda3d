#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <lucent/content.h>

namespace CtrRomTest {
// Synthetic NCSD partition with one root file; no game data. Offsets follow CTR RomFS layout.
struct Image {
    static constexpr size_t Ncch = 0x200, Romfs = 0x400, L3 = Romfs + 0x1000;
    static constexpr size_t Dir = L3 + 0x2C, File = L3 + 0x48, Data = L3 + 0x70;
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x4400);

    void word(size_t at, uint32_t value) {
        for (size_t i = 0; i < 4; ++i)
            bytes.at(at + i) = uint8_t(value >> (i * 8));
    }
    void wide(size_t at, uint64_t value) {
        word(at, uint32_t(value));
        word(at + 4, uint32_t(value >> 32));
    }
    void text(size_t at, std::string_view value) {
        std::copy(value.begin(), value.end(), bytes.begin() + at);
    }
    void hash(size_t from, size_t size, size_t to) {
        const auto digest = lucent::content::sha256(std::as_bytes(std::span(bytes).subspan(from, size)));
        std::copy(digest.begin(), digest.end(), bytes.begin() + to);
    }
    void seal() {
        hash(L3, 0x1000, Romfs + 0x3000);
        hash(Romfs + 0x3000, 0x1000, Romfs + 0x2000);
        hash(Romfs + 0x2000, 0x1000, Romfs + 0x60);
        hash(Romfs, 0x200, Ncch + 0x1E0);
    }
    explicit Image(std::string_view product = "CTR-P-AQEE", bool nested = false, char16_t filename = u'a') {
        const uint32_t dirSize = nested ? 0x34 : 0x18;
        const uint32_t fileHash = 0x2C + dirSize;
        const uint32_t fileMeta = fileHash + 4;
        const uint32_t fileData = (fileMeta + 0x24 + 15) & ~15U;
        text(0x100, "NCSD");
        word(0x104, bytes.size() / 0x200);
        word(0x120, Ncch / 0x200);
        word(0x124, (bytes.size() - Ncch) / 0x200);
        text(Ncch + 0x100, "NCCH");
        word(Ncch + 0x104, (bytes.size() - Ncch) / 0x200);
        text(Ncch + 0x150, product);
        bytes[Ncch + 0x18F] = 4;
        word(Ncch + 0x1B0, 1);
        word(Ncch + 0x1B4, 0x20);
        word(Ncch + 0x1B8, 1);
        text(Romfs, "IVFC");
        word(Romfs + 4, 0x10000);
        word(Romfs + 8, 32);
        wide(Romfs + 0x0C, 0);
        wide(Romfs + 0x14, 32);
        word(Romfs + 0x1C, 12);
        wide(Romfs + 0x24, 0x1000);
        wide(Romfs + 0x2C, 32);
        word(Romfs + 0x34, 12);
        wide(Romfs + 0x3C, 0x2000);
        wide(Romfs + 0x44, fileData + 3);
        word(Romfs + 0x4C, 12);
        word(Romfs + 0x54, 0x5C);
        word(L3, 0x28);
        word(L3 + 4, 0x28);
        word(L3 + 8, 4);
        word(L3 + 12, 0x2C);
        word(L3 + 16, dirSize);
        word(L3 + 20, fileHash);
        word(L3 + 24, 4);
        word(L3 + 28, fileMeta);
        word(L3 + 32, 0x24);
        word(L3 + 36, fileData);
        word(Dir + 4, UINT32_MAX);
        word(Dir + 8, UINT32_MAX);
        word(Dir + 16, UINT32_MAX);
        if (nested) {
            word(Dir + 8, 0x18);
            word(Dir + 12, UINT32_MAX);
            word(Dir + 16, 0x18);
            word(Dir + 0x18 + 4, UINT32_MAX);
            word(Dir + 0x18 + 8, UINT32_MAX);
            word(Dir + 0x18 + 16, UINT32_MAX);
            word(Dir + 0x18 + 20, 2);
            bytes[Dir + 0x18 + 24] = 'd';
        }
        word(L3 + fileMeta, nested ? 0x18 : 0);
        word(L3 + fileMeta + 4, UINT32_MAX);
        wide(L3 + fileMeta + 16, 3);
        word(L3 + fileMeta + 24, UINT32_MAX);
        word(L3 + fileMeta + 28, 2);
        bytes[L3 + fileMeta + 32] = uint8_t(filename);
        bytes[L3 + fileMeta + 33] = uint8_t(filename >> 8);
        text(L3 + fileData, "abc");
        seal();
    }
    void write(const std::filesystem::path& path) const {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file)
            throw std::runtime_error("cannot write synthetic CTR fixture");
    }
};
} // namespace CtrRomTest
