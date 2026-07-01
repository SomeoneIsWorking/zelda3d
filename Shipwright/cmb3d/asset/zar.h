// Parser for OoT3D ZAR archives (container holding CMB models, textures, ...).
// Port of tools/zar.py. Files inside are uncompressed. Pure C++ (no SoH/LUS deps).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Zelda3D {

struct ZarFile {
    std::string name;
    std::string type;
    uint32_t size = 0;
    uint32_t offset = 0; // offset into the ZAR blob
};

class Zar {
  public:
    // Takes ownership of the archive bytes.
    explicit Zar(std::vector<uint8_t> data);
    bool ok() const { return mOk; }
    const std::string& error() const { return mErr; }

    const std::vector<ZarFile>& files() const { return mFiles; }
    // First file whose type matches (e.g. "model"); nullptr if none.
    const ZarFile* firstOfType(const std::string& type) const;
    // First file whose name ends with the given suffix (e.g. ".cmb"); nullptr if none.
    const ZarFile* firstWithSuffix(const std::string& suffix) const;
    std::vector<uint8_t> read(const ZarFile& f) const;

  private:
    bool mOk = false;
    std::string mErr;
    std::vector<uint8_t> mData;
    std::vector<ZarFile> mFiles;
};

} // namespace Zelda3D
