#include "ctr_rom.h"
#include <cstdio>
#include <cstring>

namespace SoH3D {

static const uint64_t MEDIA = 0x200; // media unit size

static uint16_t u16(const uint8_t* b, size_t o) { uint16_t v; memcpy(&v, b + o, 2); return v; }
static uint32_t u32(const uint8_t* b, size_t o) { uint32_t v; memcpy(&v, b + o, 4); return v; }
static uint64_t u64(const uint8_t* b, size_t o) { uint64_t v; memcpy(&v, b + o, 8); return v; }
static uint64_t alignUp(uint64_t x, uint64_t a) { return (x + a - 1) & ~(a - 1); }

// Decode a UTF-16-LE name (ASCII subset) of namelen BYTES into a std::string.
static std::string utf16le(const uint8_t* b, size_t off, uint32_t namelenBytes) {
    std::string s;
    for (uint32_t i = 0; i + 1 < namelenBytes; i += 2) {
        uint16_t c = u16(b, off + i);
        s.push_back(c < 0x80 ? (char)c : '?');
    }
    return s;
}

CtrRom::CtrRom(const std::string& path) : mPath(path) {
    mFp = fopen(path.c_str(), "rb");
    if (!mFp) { fail("cannot open " + path); return; }
    if (!parseNcsd() || !parseNcch() || !parseRomfs()) return;
    mOk = true;
}

CtrRom::~CtrRom() {
    if (mFp) fclose(mFp);
}

bool CtrRom::parseNcsd() {
    uint8_t hdr[0x200];
    fseek(mFp, 0, SEEK_SET);
    if (fread(hdr, 1, sizeof(hdr), mFp) != sizeof(hdr)) return fail("short read NCSD header");
    if (memcmp(hdr + 0x100, "NCSD", 4) != 0) return fail("not an NCSD/CCI image (missing NCSD magic)");
    uint64_t off = (uint64_t)u32(hdr, 0x120) * MEDIA;
    uint64_t size = (uint64_t)u32(hdr, 0x124) * MEDIA;
    if (size == 0) return fail("partition 0 is empty");
    mNcchOff = off;
    return true;
}

bool CtrRom::parseNcch() {
    uint8_t h[0x200];
    fseek(mFp, (long)mNcchOff, SEEK_SET);
    if (fread(h, 1, sizeof(h), mFp) != sizeof(h)) return fail("short read NCCH header");
    if (memcmp(h + 0x100, "NCCH", 4) != 0) return fail("partition 0 is not an NCCH");
    mRomfsOff = mNcchOff + (uint64_t)u32(h, 0x1B0) * MEDIA;
    uint64_t romfsSize = (uint64_t)u32(h, 0x1B4) * MEDIA;
    if (romfsSize == 0) return fail("NCCH has no RomFS");
    return true;
}

bool CtrRom::parseRomfs() {
    uint8_t iv[0x60];
    fseek(mFp, (long)mRomfsOff, SEEK_SET);
    if (fread(iv, 1, sizeof(iv), mFp) != sizeof(iv)) return fail("short read IVFC header");
    if (memcmp(iv, "IVFC", 4) != 0 || u32(iv, 4) != 0x10000) return fail("RomFS IVFC header not found");
    uint32_t masterHashSize = u32(iv, 0x08);
    uint64_t lvl3Blocksize = (uint64_t)1 << u32(iv, 0x4C);
    uint64_t base = mRomfsOff + alignUp(0x60 + masterHashSize, lvl3Blocksize);
    mL3Base = base;

    uint8_t mh[0x28];
    fseek(mFp, (long)base, SEEK_SET);
    if (fread(mh, 1, sizeof(mh), mFp) != sizeof(mh)) return fail("short read romfs L3 meta header");
    mDirMetaOff = base + u32(mh, 0x0C);
    uint32_t dirMetaLen = u32(mh, 0x10);
    mFileMetaOff = base + u32(mh, 0x1C);
    uint32_t fileMetaLen = u32(mh, 0x20);
    mFileDataOff = base + u32(mh, 0x24);

    mDirMeta.resize(dirMetaLen);
    fseek(mFp, (long)mDirMetaOff, SEEK_SET);
    if (dirMetaLen && fread(mDirMeta.data(), 1, dirMetaLen, mFp) != dirMetaLen) return fail("short read dir meta");
    mFileMeta.resize(fileMetaLen);
    fseek(mFp, (long)mFileMetaOff, SEEK_SET);
    if (fileMetaLen && fread(mFileMeta.data(), 1, fileMetaLen, mFp) != fileMetaLen) return fail("short read file meta");

    walkDir(0, mRoot);
    return true;
}

void CtrRom::walkDir(uint32_t off, CtrDir& node) {
    const uint32_t INVALID = 0xFFFFFFFF;
    const uint8_t* d = mDirMeta.data();
    uint32_t child = u32(d, off + 0x08);
    uint32_t firstF = u32(d, off + 0x0C);

    // files
    const uint8_t* fm = mFileMeta.data();
    uint32_t f = firstF;
    while (f != INVALID) {
        uint32_t fsib = u32(fm, f + 0x04);
        uint64_t dataOff = u64(fm, f + 0x08);
        uint64_t dataSz = u64(fm, f + 0x10);
        uint32_t namelen = u32(fm, f + 0x1C);
        std::string name = utf16le(fm, f + 0x20, namelen);
        CtrFile fe;
        fe.name = name;
        fe.path = node.path + "/" + name;
        fe.offset = mFileDataOff + dataOff;
        fe.size = dataSz;
        node.files[name] = fe;
        f = fsib;
    }
    // child dirs
    uint32_t c = child;
    while (c != INVALID) {
        uint32_t csib = u32(d, c + 0x04);
        uint32_t cnamelen = u32(d, c + 0x14);
        std::string cname = utf16le(d, c + 0x18, cnamelen);
        auto sub = std::make_unique<CtrDir>();
        sub->name = cname;
        sub->path = node.path + "/" + cname;
        walkDir(c, *sub);
        node.dirs[cname] = std::move(sub);
        c = csib;
    }
}

const CtrFile* CtrRom::get(const std::string& path) const {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < path.size()) {
        if (path[i] == '/') { i++; continue; }
        size_t j = path.find('/', i);
        if (j == std::string::npos) j = path.size();
        parts.push_back(path.substr(i, j - i));
        i = j;
    }
    if (parts.empty()) return nullptr;
    const CtrDir* node = &mRoot;
    for (size_t k = 0; k + 1 < parts.size(); k++) {
        auto it = node->dirs.find(parts[k]);
        if (it == node->dirs.end()) return nullptr;
        node = it->second.get();
    }
    auto it = node->files.find(parts.back());
    return it == node->files.end() ? nullptr : &it->second;
}

std::vector<uint8_t> CtrRom::read(const CtrFile& fe) const {
    std::vector<uint8_t> out(fe.size);
    fseek(mFp, (long)fe.offset, SEEK_SET);
    if (fe.size && fread(out.data(), 1, fe.size, mFp) != fe.size) out.clear();
    return out;
}

std::vector<uint8_t> CtrRom::read(const std::string& path) const {
    const CtrFile* fe = get(path);
    return fe ? read(*fe) : std::vector<uint8_t>{};
}

} // namespace SoH3D
