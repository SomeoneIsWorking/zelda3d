// Minimal reader for a *decrypted* 3DS cartridge image (CCI/.3ds).
// Port of tools/ctr_romfs.py. Parses NCSD -> NCCH (partition 0) -> RomFS (IVFC
// level 3) to list/extract the game's data files (the ZAR archives) with no
// emulator/crypto. Decrypted dumps only. Pure C++ (no SoH/LUS deps).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace SoH3D {

struct CtrFile {
    std::string name;
    std::string path;   // full "/a/b/c.zar"
    uint64_t offset = 0; // absolute offset into the ROM file
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
    // Opens and parses the image. ok() is false on failure (see error()).
    explicit CtrRom(const std::string& path);
    bool ok() const { return mOk; }
    const std::string& error() const { return mErr; }

    // Find a file by absolute path ("/actor/zelda_ge1.zar"); nullptr if absent.
    const CtrFile* get(const std::string& path) const;
    // Read a file's bytes.
    std::vector<uint8_t> read(const CtrFile& fe) const;
    std::vector<uint8_t> read(const std::string& path) const;

  private:
    bool fail(const std::string& m) { mErr = m; mOk = false; return false; }
    bool parseNcsd();
    bool parseNcch();
    bool parseRomfs();
    void walkDir(uint32_t off, CtrDir& node);

    std::string mPath;
    FILE* mFp = nullptr;
    bool mOk = false;
    std::string mErr;

    uint64_t mNcchOff = 0, mRomfsOff = 0;
    uint64_t mL3Base = 0, mDirMetaOff = 0, mFileMetaOff = 0, mFileDataOff = 0;
    std::vector<uint8_t> mDirMeta, mFileMeta;
    CtrDir mRoot;

  public:
    ~CtrRom();
};

} // namespace SoH3D
