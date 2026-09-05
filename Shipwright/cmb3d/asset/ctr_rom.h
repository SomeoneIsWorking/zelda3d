// Reader for decrypted NCSD/NCCH cartridge RomFS assets. No title policy or crypto keys.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Zelda3D {
struct CtrFile {
    std::string name;
    std::string path;
    uint64_t offset = 0;
    uint64_t size = 0;
};
struct CtrDir {
    std::string name;
    std::string path;
    std::unordered_map<std::string, std::unique_ptr<CtrDir>> dirs;
    std::unordered_map<std::string, CtrFile> files;
};
class CtrRom {
  public:
    struct ValidationSummary {
        bool integrityVerified = false;
        uint64_t imageBytes = 0, directories = 0, files = 0, fileBytes = 0;
        uint64_t verifiedBlocks = 0, verifiedBytes = 0;
    };
    // Parses every metadata record and validates the directory/hash graph and byte ranges.
    explicit CtrRom(const std::filesystem::path& path);
    CtrRom(const CtrRom&) = delete;
    CtrRom& operator=(const CtrRom&) = delete;
    bool ok() const {
        return mOk;
    }
    const std::string& error() const {
        return mErr;
    }
    // Reads and verifies the NCCH RomFS superblock and all IVFC hash/data blocks including padding.
    // Proves readable, internally consistent assets, not Nintendo RSA authenticity, executable or
    // other-partition integrity, title identity, or immunity to later external mutation.
    // Failure invalidates this reader; no partial validation is reported as success.
    bool validateContent();
    ValidationSummary validationSummary() const {
        return mOk ? mSummary : ValidationSummary{};
    }
    const CtrFile* get(const std::string& path) const;
    std::vector<uint8_t> read(const CtrFile& fe) const;
    std::vector<uint8_t> read(const std::string& path) const;

  private:
    struct Level {
        uint64_t offset = 0, size = 0, blockSize = 0;
    };
    bool fail(const std::string& message);
    bool readAt(uint64_t offset, std::span<uint8_t> bytes) const;
    bool parseNcsd();
    bool parseNcch();
    bool parseRomfs();
    bool parseDirectoryTree();
    mutable std::ifstream mStream;
    bool mOk = false;
    bool mEncryptionFlag = false;
    std::string mErr;
    uint64_t mImageSize = 0, mNcchOff = 0, mNcchSize = 0, mRomfsOff = 0, mRomfsSize = 0;
    uint64_t mSuperblockSize = 0, mMasterHashSize = 0, mFileDataOff = 0;
    std::array<uint8_t, 32> mSuperblockHash{};
    std::array<Level, 3> mLevels{};
    ValidationSummary mSummary;
    std::vector<uint8_t> mDirMeta, mFileMeta, mDirHash, mFileHash;
    CtrDir mRoot;
};
} // namespace Zelda3D
