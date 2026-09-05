#include "ctr_rom.h"
#include <algorithm>
#include <filesystem>
#include <limits>
#include <set>
#include <utility>
#include <lucent/content.h>

namespace Zelda3D {
namespace {
constexpr uint64_t Media = 0x200;
constexpr uint32_t Invalid = UINT32_MAX;
constexpr uint64_t MetadataBudget = 64 * 1024 * 1024;
constexpr size_t MaxDirectoryDepth = 128;
uint32_t u32(std::span<const uint8_t> b, size_t o) {
    return uint32_t(b[o]) | (uint32_t(b[o + 1]) << 8) | (uint32_t(b[o + 2]) << 16) | (uint32_t(b[o + 3]) << 24);
}
uint64_t u64(std::span<const uint8_t> b, size_t o) {
    return u32(b, o) | (uint64_t(u32(b, o + 4)) << 32);
}
bool fits(uint64_t offset, uint64_t size, uint64_t extent) {
    return offset <= extent && size <= extent - offset;
}
uint64_t aligned(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}
bool magic(std::span<const uint8_t> bytes, size_t offset, const char* expected) {
    return std::equal(bytes.begin() + offset, bytes.begin() + offset + 4, expected);
}
// Decode losslessly: replacing non-ASCII with '?' aliases distinct file identities.
bool nameAt(std::span<const uint8_t> data, size_t offset, uint32_t length, std::string& result) {
    if ((length & 1) || length > 510 || !fits(offset, length, data.size()))
        return false;
    for (size_t i = 0; i < length; i += 2) {
        uint32_t c = data[offset + i] | (uint32_t(data[offset + i + 1]) << 8);
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 3 >= length)
                return false;
            const uint32_t low = data[offset + i + 2] | (uint32_t(data[offset + i + 3]) << 8);
            if (low < 0xDC00 || low > 0xDFFF)
                return false;
            c = 0x10000 + ((c - 0xD800) << 10) + low - 0xDC00;
            i += 2;
        } else if (c >= 0xDC00 && c <= 0xDFFF)
            return false;
        if (c == 0 || c == '/' || c == '\\')
            return false;
        if (c < 0x80)
            result.push_back(static_cast<char>(c));
        else {
            if (c < 0x800)
                result.push_back(static_cast<char>(0xC0 | (c >> 6)));
            else {
                if (c < 0x10000)
                    result.push_back(static_cast<char>(0xE0 | (c >> 12)));
                else {
                    result.push_back(static_cast<char>(0xF0 | (c >> 18)));
                    result.push_back(static_cast<char>(0x80 | ((c >> 12) & 63)));
                }
                result.push_back(static_cast<char>(0x80 | ((c >> 6) & 63)));
            }
            result.push_back(static_cast<char>(0x80 | (c & 63)));
        }
    }
    return result != "." && result != "..";
}
struct Record {
    uint32_t parent, sibling, hashNext;
    std::string name;
    uint32_t nameHash;
};
using Records = std::unordered_map<uint32_t, Record>;
bool recordsFrom(const std::vector<uint8_t>& data, bool directory, Records& records) {
    const uint32_t fixedSize = directory ? 0x18 : 0x20;
    for (uint64_t offset = 0; offset < data.size();) {
        if (!fits(offset, fixedSize, data.size()))
            return false;
        const uint32_t length = u32(data, offset + fixedSize - 4);
        const uint64_t size = aligned(fixedSize + uint64_t(length), 4);
        if (!fits(offset, size, data.size()))
            return false;
        Record record{ u32(data, offset), u32(data, offset + 4), u32(data, offset + fixedSize - 8), {}, 0 };
        if (!nameAt(data, offset + fixedSize, length, record.name))
            return false;
        if (record.name.empty() && !(directory && offset == 0))
            return false;
        record.nameHash = record.parent ^ 123456789;
        for (uint32_t i = 0; i < length; i += 2) {
            const size_t at = offset + fixedSize + i;
            record.nameHash = ((record.nameHash >> 5) | (record.nameHash << 27)) ^
                              (uint32_t(data[at]) | (uint32_t(data[at + 1]) << 8));
        }
        records.emplace(static_cast<uint32_t>(offset), std::move(record));
        offset += size;
    }
    return true;
}
bool validHashChains(const std::vector<uint8_t>& buckets, const Records& records) {
    if (buckets.empty() || (buckets.size() & 3))
        return false;
    std::set<uint32_t> visited;
    const size_t count = buckets.size() / 4;
    for (size_t i = 0; i < count; ++i) {
        for (uint32_t offset = u32(buckets, i * 4); offset != Invalid;) {
            const auto entry = records.find(offset);
            if (entry == records.end() || !visited.insert(offset).second || entry->second.nameHash % count != i)
                return false;
            offset = entry->second.hashNext;
        }
    }
    return visited.size() == records.size();
}
} // namespace

bool CtrRom::fail(const std::string& message) {
    mErr = message;
    mOk = false;
    return false;
}
CtrRom::CtrRom(const std::filesystem::path& path) : mStream(path, std::ios::binary) {
    if (!mStream) {
        fail("cannot open cartridge image");
        return;
    }
    mStream.seekg(0, std::ios::end);
    const auto size = mStream.tellg();
    if (size < 0) {
        fail("cannot measure cartridge image");
        return;
    }
    mImageSize = static_cast<uint64_t>(size);
    if (parseNcsd() && parseNcch() && parseRomfs()) {
        mOk = true;
        // Some decryptors retain the original encryption flag. Accept these only after the full
        // plaintext RomFS hash chain proves the consumed bytes are already decrypted.
        if (mEncryptionFlag)
            validateContent();
    }
}
bool CtrRom::readAt(uint64_t offset, std::span<uint8_t> bytes) const {
    if (!fits(offset, bytes.size(), mImageSize) || offset > uint64_t(std::numeric_limits<std::streamoff>::max()) ||
        bytes.size() > uint64_t(std::numeric_limits<std::streamsize>::max()))
        return false;
    mStream.clear();
    mStream.seekg(static_cast<std::streamoff>(offset));
    if (!mStream)
        return false;
    mStream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return mStream.good();
}
bool CtrRom::parseNcsd() {
    std::array<uint8_t, 0x200> h{};
    if (!readAt(0, h) || !magic(h, 0x100, "NCSD"))
        return fail("missing or truncated NCSD header");
    const uint64_t declared = uint64_t(u32(h, 0x104)) * Media;
    if (declared < Media || mImageSize > declared)
        return fail("invalid NCSD media size");
    std::vector<std::pair<uint64_t, uint64_t>> partitions;
    for (size_t i = 0; i < 8; ++i) {
        const uint64_t off = uint64_t(u32(h, 0x120 + i * 8)) * Media;
        const uint64_t size = uint64_t(u32(h, 0x124 + i * 8)) * Media;
        if (size == 0 && off == 0 && i != 0)
            continue;
        if (off < Media || size < Media || !fits(off, size, mImageSize))
            return fail("NCSD partition is missing, truncated or out of range");
        partitions.emplace_back(off, off + size);
        if (i == 0) {
            mNcchOff = off;
            mNcchSize = size;
        }
    }
    std::sort(partitions.begin(), partitions.end());
    for (size_t i = 1; i < partitions.size(); ++i)
        if (partitions[i].first < partitions[i - 1].second)
            return fail("overlapping NCSD partitions");
    return true;
}
bool CtrRom::parseNcch() {
    std::array<uint8_t, 0x200> h{};
    if (!readAt(mNcchOff, h) || !magic(h, 0x100, "NCCH"))
        return fail("missing or truncated NCCH header");
    mEncryptionFlag = !(h[0x18F] & 4);
    if (h[0x18E] > 10)
        return fail("unsupported NCCH media-unit size");
    const uint64_t unit = Media << h[0x18E];
    const uint64_t contentSize = uint64_t(u32(h, 0x104)) * unit;
    if (contentSize < Media || contentSize > mNcchSize)
        return fail("NCCH content exceeds its partition");
    const uint64_t offset = uint64_t(u32(h, 0x1B0)) * unit;
    mRomfsSize = uint64_t(u32(h, 0x1B4)) * unit;
    mSuperblockSize = uint64_t(u32(h, 0x1B8)) * unit;
    if (offset < Media || !mRomfsSize || !fits(offset, mRomfsSize, contentSize))
        return fail("NCCH RomFS is absent or out of range");
    if (!mSuperblockSize || mSuperblockSize > MetadataBudget || mSuperblockSize > mRomfsSize)
        return fail("invalid NCCH RomFS hash region");
    mRomfsOff = mNcchOff + offset;
    std::copy_n(h.begin() + 0x1E0, mSuperblockHash.size(), mSuperblockHash.begin());
    return true;
}
bool CtrRom::parseRomfs() {
    std::array<uint8_t, 0x60> iv{};
    if (mRomfsSize < iv.size() || !readAt(mRomfsOff, iv) || !magic(iv, 0, "IVFC") || u32(iv, 4) != 0x10000 ||
        u32(iv, 0x54) != 0x5C)
        return fail(mEncryptionFlag ? "encrypted or invalid RomFS; provide a decrypted image"
                                    : "invalid RomFS IVFC header");
    mMasterHashSize = u32(iv, 8);
    if (!mMasterHashSize || mMasterHashSize > MetadataBudget || (mMasterHashSize & 31) ||
        !fits(0x60, mMasterHashSize, mSuperblockSize))
        return fail("invalid IVFC master hash region");
    for (size_t i = 0; i < 3; ++i) {
        const uint32_t shift = u32(iv, 0x1C + 0x18 * i);
        if (shift < 5 || shift > 20)
            return fail("unsupported IVFC block size");
        mLevels[i].blockSize = uint64_t(1) << shift;
        mLevels[i].size = u64(iv, 0x14 + 0x18 * i);
        if (!mLevels[i].size || mLevels[i].size > mRomfsSize)
            return fail("invalid IVFC level extent");
    }
    // Physical order: header/master hashes, level 3, level 1, level 2. See GodMode9
    // game/romfs.c GetRomFsLvOffset and Project_CTR ctrtool/IvfcProcess.cpp.
    mLevels[2].offset = aligned(0x60 + mMasterHashSize, mLevels[2].blockSize);
    mLevels[0].offset = mLevels[2].offset + aligned(mLevels[2].size, mLevels[2].blockSize);
    mLevels[1].offset = mLevels[0].offset + aligned(mLevels[0].size, mLevels[0].blockSize);
    for (size_t i = 0; i < 3; ++i) {
        const auto& level = mLevels[i];
        const uint64_t padded = aligned(level.size, level.blockSize);
        const uint64_t hashes = (padded / level.blockSize) * 32;
        if (!fits(level.offset, padded, mRomfsSize) || hashes != (i == 0 ? mMasterHashSize : mLevels[i - 1].size))
            return fail("IVFC level range or hash coverage is inconsistent");
    }
    std::array<uint8_t, 0x28> h{};
    const uint64_t base = mRomfsOff + mLevels[2].offset;
    if (mLevels[2].size < h.size() || !readAt(base, h) || u32(h, 0) != h.size())
        return fail("invalid RomFS metadata header");
    std::array<std::vector<uint8_t>*, 4> tables{ &mDirHash, &mDirMeta, &mFileHash, &mFileMeta };
    uint64_t end = h.size(), total = 0;
    for (size_t i = 0; i < tables.size(); ++i) {
        const uint64_t offset = u32(h, 4 + i * 8), size = u32(h, 8 + i * 8);
        total += size;
        if (offset < end || (offset & 3) || total > MetadataBudget || !fits(offset, size, mLevels[2].size))
            return fail("RomFS metadata is overlapping, excessive or out of range");
        tables[i]->resize(size);
        if (!readAt(base + offset, *tables[i]))
            return fail("unreadable RomFS metadata");
        end = offset + size;
    }
    const uint64_t dataOffset = u32(h, 0x24);
    if (dataOffset < end || dataOffset > mLevels[2].size)
        return fail("invalid RomFS file-data offset");
    mFileDataOff = base + dataOffset;
    return parseDirectoryTree();
}
bool CtrRom::parseDirectoryTree() {
    Records dirs, files;
    if (!recordsFrom(mDirMeta, true, dirs) || !recordsFrom(mFileMeta, false, files) || !dirs.contains(0) ||
        !dirs.at(0).name.empty() || dirs.at(0).parent != 0 || dirs.at(0).sibling != Invalid)
        return fail("malformed RomFS metadata records or root");
    if (!validHashChains(mDirHash, dirs) || !validHashChains(mFileHash, files))
        return fail("invalid, incomplete or cyclic RomFS hash chains");
    struct Pending {
        uint32_t offset;
        CtrDir* node;
        size_t depth;
    };
    std::vector<Pending> pending{ { 0, &mRoot, 0 } };
    std::set<uint32_t> seenDirs{ 0 }, seenFiles;
    std::vector<std::pair<uint64_t, uint64_t>> fileRanges;
    const uint64_t dataSize = mRomfsOff + mLevels[2].offset + mLevels[2].size - mFileDataOff;
    while (!pending.empty()) {
        const auto [offset, node, depth] = pending.back();
        pending.pop_back();
        if (depth > MaxDirectoryDepth)
            return fail("RomFS directory depth exceeds reader budget");
        for (uint32_t f = u32(mDirMeta, offset + 12); f != Invalid;) {
            const auto it = files.find(f);
            if (it == files.end() || !seenFiles.insert(f).second || it->second.parent != offset)
                return fail("invalid, shared or cyclic RomFS file link");
            const auto& record = it->second;
            const uint64_t off = u64(mFileMeta, f + 8), size = u64(mFileMeta, f + 16);
            if (!fits(off, size, dataSize))
                return fail("RomFS file bytes exceed data region");
            mSummary.fileBytes += size;
            if (size)
                fileRanges.emplace_back(off, off + size);
            CtrFile file{ record.name, node->path + "/" + record.name, mFileDataOff + off, size };
            if (!node->files.emplace(record.name, std::move(file)).second)
                return fail("duplicate RomFS file name");
            f = record.sibling;
        }
        for (uint32_t d = u32(mDirMeta, offset + 8); d != Invalid;) {
            const auto it = dirs.find(d);
            if (it == dirs.end() || !seenDirs.insert(d).second || it->second.parent != offset)
                return fail("invalid, shared or cyclic RomFS directory link");
            const auto& record = it->second;
            auto child = std::make_unique<CtrDir>();
            child->name = record.name;
            child->path = node->path + "/" + record.name;
            if (node->files.contains(record.name))
                return fail("RomFS file/directory name collision");
            const auto [inserted, success] = node->dirs.emplace(record.name, std::move(child));
            if (!success)
                return fail("duplicate RomFS directory name");
            pending.push_back({ d, inserted->second.get(), depth + 1 });
            d = record.sibling;
        }
    }
    if (seenDirs.size() != dirs.size() || seenFiles.size() != files.size())
        return fail("unreachable RomFS records");
    std::sort(fileRanges.begin(), fileRanges.end());
    for (size_t i = 1; i < fileRanges.size(); ++i)
        if (fileRanges[i].first < fileRanges[i - 1].second)
            return fail("overlapping RomFS file bytes");
    mSummary.imageBytes = mImageSize;
    mSummary.directories = seenDirs.size();
    mSummary.files = seenFiles.size();
    return true;
}
bool CtrRom::validateContent() {
    if (!mOk)
        return false;
    mSummary.integrityVerified = false;
    mSummary.verifiedBlocks = 0;
    mSummary.verifiedBytes = 0;
    std::vector<uint8_t> superblock(mSuperblockSize);
    if (!readAt(mRomfsOff, superblock))
        return fail("unreadable RomFS superblock");
    if (lucent::content::sha256(std::as_bytes(std::span(superblock))) != mSuperblockHash)
        return fail("NCCH RomFS superblock SHA-256 mismatch");
    for (size_t i = 0; i < mLevels.size(); ++i) {
        const auto& level = mLevels[i];
        const uint64_t hashOffset = mRomfsOff + (i == 0 ? 0x60 : mLevels[i - 1].offset);
        std::vector<uint8_t> block(level.blockSize);
        for (uint64_t position = 0, index = 0; position < level.size; position += level.blockSize, ++index) {
            std::array<uint8_t, 32> expected{};
            if (!readAt(hashOffset + index * 32, expected) || !readAt(mRomfsOff + level.offset + position, block))
                return fail("unreadable IVFC level " + std::to_string(i + 1));
            if (lucent::content::sha256(std::as_bytes(std::span(block))) != expected)
                return fail("IVFC SHA-256 mismatch at level " + std::to_string(i + 1) + " block " +
                            std::to_string(index));
            ++mSummary.verifiedBlocks;
            mSummary.verifiedBytes += block.size();
        }
    }
    mSummary.verifiedBytes += mSuperblockSize;
    mSummary.integrityVerified = true;
    return true;
}
const CtrFile* CtrRom::get(const std::string& path) const {
    if (!mOk)
        return nullptr;
    const CtrDir* node = &mRoot;
    size_t at = 0;
    while (at < path.size()) {
        if (path[at] == '/') {
            ++at;
            continue;
        }
        const size_t next = path.find('/', at);
        if (next == std::string::npos) {
            const auto it = node->files.find(path.substr(at));
            return it == node->files.end() ? nullptr : &it->second;
        }
        const auto it = node->dirs.find(path.substr(at, next - at));
        if (it == node->dirs.end())
            return nullptr;
        node = it->second.get();
        at = next + 1;
    }
    return nullptr;
}
std::vector<uint8_t> CtrRom::read(const CtrFile& file) const {
    const CtrFile* owned = get(file.path);
    if (!owned || owned->offset != file.offset || owned->size != file.size ||
        file.size > std::vector<uint8_t>().max_size())
        return {};
    std::vector<uint8_t> bytes(file.size);
    if (!readAt(file.offset, bytes))
        return {};
    return bytes;
}
std::vector<uint8_t> CtrRom::read(const std::string& path) const {
    const CtrFile* file = get(path);
    return file ? read(*file) : std::vector<uint8_t>{};
}
} // namespace Zelda3D
