// Decode PICA200 (3DS) textures to RGBA8. Port of tools/pica_texture.py
// (itself a port of noclip oot3d/pica_texture.ts). Pure C++ (no SoH/LUS deps).
#pragma once
#include <cstdint>
#include <vector>

namespace Zelda3D {

// Decode raw texture bytes of the given glFormat ((dataType<<16)|formatConstant)
// into width*height*4 RGBA8 bytes. Returns empty on unknown/short input.
std::vector<uint8_t> PicaDecode(uint32_t glFormat, int width, int height, const std::vector<uint8_t>& data);

// Reproduce the exact byte buffer Citra/Azahar feed to their legacy custom-texture hash
// (CityHash64 over DecodeTexture(..., converted=false): morton-detiled, vertically flipped
// to GL bottom-up, width*height*bpp). The 5 plain color formats (RGBA8/RGB8/RGB565/RGBA5551/
// RGBA4) keep native bytes; every other format is decoded to RGBA8 (R,G,B,A). Returns empty
// on unknown/short input. Used to look up replacement PNGs by their Citra legacy hash.
std::vector<uint8_t> PicaLegacyHashBytes(uint32_t glFormat, int width, int height,
                                         const std::vector<uint8_t>& data);

} // namespace Zelda3D
