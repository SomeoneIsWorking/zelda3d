// CityHash64 — public-domain port of Google CityHash v1.1 (the variant Citra/Azahar
// use as Common::ComputeHash64). Used to match the legacy (use_new_hash=false) custom
// texture hashes in Henriko's OoT3D 4K pack.
#pragma once
#include <cstddef>
#include <cstdint>

namespace Zelda3D {
uint64_t CityHash64(const char* buf, size_t len);
}
