// Grezzo LzS ("LzS\1") decompressor — the Lempel-Ziv variant Grezzo wraps some MM3D
// actor archives with. About 40% of /actors/zelda2_*.gar.lzs archives are actually
// LzS-compressed GAR2 payloads; the rest carry the .gar.lzs extension but are raw GAR2.
//
// Algorithm: LZSS with a 4096-byte ring dictionary preinitialized to zero and initial
// write index 0xFEE. Control byte holds 8 flags (LSB-first): 1 = literal (copy 1 byte,
// store in dict), 0 = back-ref (2 bytes: low 8 bits of readidx, then (hi4<<4)|len_minus3).
//
// Verified against MM3D /actors/zelda2_boj.gar.lzs (148907 -> 217088 bytes) and
// zelda2_box.gar.lzs (90642 -> 135200 bytes); both round-trip a valid GAR2 header.
//
// Pure C++ (no SoH/LUS deps). Sibling of gar.h / zar.h under cmb3d/asset/.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Zelda3D {

// True iff `data` starts with the LzS\1 magic and has a plausible 16-byte header.
bool LzsIsCompressed(const std::vector<uint8_t>& data);

// Decompress an LzS-wrapped buffer. On success returns the raw payload (typically a
// GAR2 archive). On failure returns empty and writes a reason into `err` (if non-null).
std::vector<uint8_t> LzsDecompress(const std::vector<uint8_t>& data, std::string* err = nullptr);

} // namespace Zelda3D
