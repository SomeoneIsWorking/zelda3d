#include "gar.h"
#include <cstring>

// GAR version-2 layout (decoded from MM3D /actors/zelda2_bh.gar.lzs; see docs/MM_NATIVE.md
// "N4.2b BLOCKER FOUND"):
//
//   Header (0x20 bytes):
//     0x00 char[4] "GAR\2"
//     0x04 u32     fileSize (matches the blob length)
//     0x08 u16     nTypes
//     0x0A u16     nFiles
//     0x0C u32     typesOff   (usually 0x20)
//     0x10 u32     filesOff   (file-entry table)
//     0x14 u32     dataHdrOff (per-file data-offset table)
//     0x18 char[8] codec name ("jenkins") — 8-byte field is what makes the GAR2 header
//                  0x20 vs the ZAR header's 0x18.
//   Types table @typesOff, 0x10/entry: { u32 count, u32 idxOff(-1=none), u32 nameOff, u32 pad }
//     idxOff -> array of u32 file indices belonging to that type.
//   Files table @filesOff, 0x0C/entry: { u32 size, u32 shortNameOff, u32 fullPathOff }
//   Data-offset table @dataHdrOff, 4/entry: { u32 dataOffset } — where file i's bytes live.
//   (Names are inline NUL-terminated C-strings referenced by the *Off fields.)
namespace Zelda3D {

static uint16_t u16(const uint8_t* b, size_t o) { uint16_t v; memcpy(&v, b + o, 2); return v; }
static uint32_t u32(const uint8_t* b, size_t o) { uint32_t v; memcpy(&v, b + o, 4); return v; }
static std::string cstr(const uint8_t* b, size_t o, size_t cap) {
    std::string s;
    for (size_t i = o; i < cap && b[i]; i++) s.push_back((char)b[i]);
    return s;
}

Gar::Gar(std::vector<uint8_t> data) : mData(std::move(data)) {
    const uint8_t* b = mData.data();
    size_t n = mData.size();
    if (n < 0x20 || memcmp(b, "GAR\x02", 4) != 0) { mErr = "not a GAR2 archive"; return; }
    uint16_t nTypes = u16(b, 0x08);
    uint16_t nFiles = u16(b, 0x0A);
    uint32_t typesOff = u32(b, 0x0C);
    uint32_t filesOff = u32(b, 0x10);
    uint32_t dataHdrOff = u32(b, 0x14);

    // Bounds: the three tables must fit within the blob.
    if ((size_t)typesOff + 16 * (size_t)nTypes > n) { mErr = "types table out of range"; return; }
    if ((size_t)filesOff + 12 * (size_t)nFiles > n) { mErr = "files table out of range"; return; }
    if ((size_t)dataHdrOff + 4 * (size_t)nFiles > n) { mErr = "data table out of range"; return; }

    mFiles.resize(nFiles);
    for (uint16_t i = 0; i < nFiles; i++) {
        uint32_t fe = filesOff + 12 * i;
        uint32_t fsize = u32(b, fe);
        uint32_t nameStr = u32(b, fe + 4);
        uint32_t pathStr = u32(b, fe + 8);
        uint32_t off = u32(b, dataHdrOff + 4 * i);
        mFiles[i].name = cstr(b, nameStr, n);
        mFiles[i].path = cstr(b, pathStr, n);
        mFiles[i].size = fsize;
        mFiles[i].offset = off;
    }
    for (uint16_t t = 0; t < nTypes; t++) {
        uint32_t e = typesOff + 16 * t;
        uint32_t cnt = u32(b, e);
        uint32_t idxOff = u32(b, e + 4);
        std::string tname = cstr(b, u32(b, e + 8), n);
        if (idxOff == 0xFFFFFFFF) continue;
        if ((size_t)idxOff + 4 * (size_t)cnt > n) continue;
        for (uint32_t k = 0; k < cnt; k++) {
            uint32_t fi = u32(b, idxOff + 4 * k);
            if (fi < mFiles.size()) mFiles[fi].type = tname;
        }
    }
    mOk = true;
}

const GarFile* Gar::firstOfType(const std::string& type) const {
    for (const auto& f : mFiles)
        if (f.type == type) return &f;
    return nullptr;
}

const GarFile* Gar::firstWithSuffix(const std::string& suffix) const {
    for (const auto& f : mFiles)
        if (f.path.size() >= suffix.size() &&
            f.path.compare(f.path.size() - suffix.size(), suffix.size(), suffix) == 0)
            return &f;
    return nullptr;
}

std::vector<uint8_t> Gar::read(const GarFile& f) const {
    if ((size_t)f.offset + f.size > mData.size()) return {};
    return std::vector<uint8_t>(mData.begin() + f.offset, mData.begin() + f.offset + f.size);
}

} // namespace Zelda3D
