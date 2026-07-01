#include "zar.h"
#include <cstring>

namespace Zelda3D {

static uint16_t u16(const uint8_t* b, size_t o) { uint16_t v; memcpy(&v, b + o, 2); return v; }
static uint32_t u32(const uint8_t* b, size_t o) { uint32_t v; memcpy(&v, b + o, 4); return v; }
static std::string cstr(const uint8_t* b, size_t o, size_t cap) {
    std::string s;
    for (size_t i = o; i < cap && b[i]; i++) s.push_back((char)b[i]);
    return s;
}

Zar::Zar(std::vector<uint8_t> data) : mData(std::move(data)) {
    const uint8_t* b = mData.data();
    size_t n = mData.size();
    if (n < 0x18 || memcmp(b, "ZAR\x01", 4) != 0) { mErr = "not a ZAR archive"; return; }
    uint16_t nTypes = u16(b, 0x08);
    uint16_t nFiles = u16(b, 0x0A);
    uint32_t typesOff = u32(b, 0x0C);
    uint32_t metaOff = u32(b, 0x10);
    uint32_t dataOff = u32(b, 0x14);

    mFiles.resize(nFiles);
    for (uint16_t i = 0; i < nFiles; i++) {
        uint32_t off = u32(b, dataOff + 4 * i);   // per-file data offset
        uint32_t m = metaOff + 8 * i;
        uint32_t fsize = u32(b, m);
        uint32_t nameStr = u32(b, m + 4);
        mFiles[i].name = cstr(b, nameStr, n);
        mFiles[i].size = fsize;
        mFiles[i].offset = off;
    }
    for (uint16_t t = 0; t < nTypes; t++) {
        uint32_t e = typesOff + 16 * t;
        uint32_t cnt = u32(b, e);
        uint32_t idxOff = u32(b, e + 4);
        std::string tname = cstr(b, u32(b, e + 8), n);
        if (idxOff == 0xFFFFFFFF) continue;
        for (uint32_t k = 0; k < cnt; k++) {
            uint32_t fi = u32(b, idxOff + 4 * k);
            if (fi < mFiles.size()) mFiles[fi].type = tname;
        }
    }
    mOk = true;
}

const ZarFile* Zar::firstOfType(const std::string& type) const {
    for (const auto& f : mFiles)
        if (f.type == type) return &f;
    return nullptr;
}

const ZarFile* Zar::firstWithSuffix(const std::string& suffix) const {
    for (const auto& f : mFiles)
        if (f.name.size() >= suffix.size() &&
            f.name.compare(f.name.size() - suffix.size(), suffix.size(), suffix) == 0)
            return &f;
    return nullptr;
}

std::vector<uint8_t> Zar::read(const ZarFile& f) const {
    if ((size_t)f.offset + f.size > mData.size()) return {};
    return std::vector<uint8_t>(mData.begin() + f.offset, mData.begin() + f.offset + f.size);
}

} // namespace Zelda3D
