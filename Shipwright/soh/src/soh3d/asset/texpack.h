// Hi-res custom texture pack lookup (Citra/Azahar-format packs, e.g. Henriko's OoT3D 4K).
// Replaces a CMB texture's decoded RGBA with a same-content hi-res PNG, located by the
// texture's Citra legacy hash (see PicaLegacyHashBytes + CityHash64). The pack is a
// user-provided drop-in (gitignored, ~GB); when absent the feature is simply inactive.
#pragma once
#include <cstdint>
#include <vector>

namespace SoH3D {

// On a hit, fills `rgba` with W*H*4 top-down RGBA8 of the replacement and sets w/h, then
// returns true. The pack is indexed lazily on first call. Returns false (no replacement)
// when no pack is found or the hash is absent.
bool TexPackLookup(uint64_t hash, int& w, int& h, std::vector<uint8_t>& rgba);

} // namespace SoH3D
