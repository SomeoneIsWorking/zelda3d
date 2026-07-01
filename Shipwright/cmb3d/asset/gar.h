// Parser for Grezzo GAR version-2 archives ("GAR\2"), the container MM3D (Majora's Mask
// 3D) uses for actor models under /actors/zelda2_*.gar.lzs. Sibling of the OoT3D ZAR
// parser (zar.h): same idea (types table + file table + inline C-string names), different
// header layout. Files inside are uncompressed; the CMB/CSAB payloads are the same 3DS
// formats the shared Cmb/Csab parsers handle. Pure C++ (no SoH/LUS deps).
//
// NOTE: some MM3D actor archives are LzS-compressed (magic "LzS\1") despite also carrying
// the .gar.lzs extension; Gar only parses raw GAR2. Decompress LzS before constructing Gar.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Zelda3D {

struct GarFile {
    std::string name;     // short name, e.g. "skylark"
    std::string path;     // full inline path, e.g. "model/skylark.cmb"
    std::string type;     // GAR type name, e.g. "cmb" / "csab"
    uint32_t size = 0;
    uint32_t offset = 0;  // offset into the GAR blob
};

class Gar {
  public:
    // Takes ownership of the archive bytes (must already be raw GAR2, not LzS).
    explicit Gar(std::vector<uint8_t> data);
    bool ok() const { return mOk; }
    const std::string& error() const { return mErr; }

    const std::vector<GarFile>& files() const { return mFiles; }
    // First file whose type matches (e.g. "cmb"); nullptr if none.
    const GarFile* firstOfType(const std::string& type) const;
    // First file whose full path ends with the given suffix (e.g. ".cmb"); nullptr if none.
    const GarFile* firstWithSuffix(const std::string& suffix) const;
    std::vector<uint8_t> read(const GarFile& f) const;

  private:
    bool mOk = false;
    std::string mErr;
    std::vector<uint8_t> mData;
    std::vector<GarFile> mFiles;
};

} // namespace Zelda3D
