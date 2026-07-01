// Reader for OoT3D CTXB standalone-texture containers (the engine-billboarded
// sprites: sun/moon/lens-flare discs in /kankyo/BlueSky.zar). A CTXB is a thin
// wrapper around the SAME "tex " chunk a CMB carries — header (0x18 bytes) then a
// byte-identical tex chunk at file offset 0x18 — so each texture entry decodes via
// the existing PICA decoder (pica_texture.cpp), glFormat = (data_type<<16)|fmt.
// Pure C++ (no SoH/LUS deps). Used for #28e (sun/moon discs) where the asset has no
// CMB to hang the texture on. Port of the noclip oot3d/CTXB.ts layout.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Zelda3D {

struct CtxbTexture {
    std::string name;
    int width = 0, height = 0;
    uint16_t fmt = 0, data_type = 0;
    uint32_t data_offset = 0, data_len = 0; // data_offset is relative to the file's tex-data section
    uint32_t glFormat() const { return ((uint32_t)data_type << 16) | fmt; }
};

class Ctxb {
  public:
    explicit Ctxb(std::vector<uint8_t> data);
    bool ok() const { return mOk; }
    const std::string& error() const { return mErr; }

    const std::vector<CtxbTexture>& textures() const { return mTextures; }
    // Raw (still-encoded) bytes of a texture, sliced from the CTXB tex-data block.
    std::vector<uint8_t> textureRaw(const CtxbTexture& t) const;
    // Decode texture i to RGBA8 (w*h*4, row 0 = top, same convention as CMB textures).
    // Returns empty on out-of-range / unknown format. Fills *w/*h with the dimensions.
    std::vector<uint8_t> decodeRGBA(size_t i, int* w, int* h) const;

  private:
    bool mOk = false;
    std::string mErr;
    std::vector<uint8_t> mData;
    uint32_t mTexDataOff = 0; // file offset of the texel data section
    std::vector<CtxbTexture> mTextures;
};

} // namespace Zelda3D
