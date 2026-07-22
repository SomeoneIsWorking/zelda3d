// Hi-res custom texture pack lookup (Citra/Azahar-format packs, e.g. Henriko's OoT3D 4K).
// Replaces a CMB texture's decoded RGBA with a same-content hi-res PNG, located by the
// texture's Citra legacy hash (see PicaLegacyHashBytes + CityHash64). The pack is a
// user-provided drop-in (gitignored, ~GB); when absent the feature is simply inactive.
#pragma once
#include <cstdint>
#include <vector>

namespace Zelda3D {

// On a hit, fills `rgba` with W*H*4 top-down RGBA8 of the replacement and sets w/h, then
// returns true. The pack is indexed lazily on first call. Returns false (no replacement)
// when no pack is found or the hash is absent.
bool TexPackLookup(uint64_t hash, int& w, int& h, std::vector<uint8_t>& rgba);

// Whether a pack was found (only meaningful after the first lookup), how many textures
// are indexed, and how many lookups hit / missed it. The parity harness reports these so
// "hi-res is actually in effect" is a measurement rather than an assumption.
struct TexPackStats {
    bool scanned;
    bool active;
    uint64_t indexed;
    uint64_t hits;
    uint64_t misses;
};
TexPackStats TexPackGetStats();

} // namespace Zelda3D
