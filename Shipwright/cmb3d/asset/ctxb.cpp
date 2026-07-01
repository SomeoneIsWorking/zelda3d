#include "ctxb.h"
#include "pica_texture.h"
#include <cstring>

namespace Zelda3D {

static uint16_t u16(const uint8_t* b, size_t o) { uint16_t v; memcpy(&v, b + o, 2); return v; }
static uint32_t u32(const uint8_t* b, size_t o) { uint32_t v; memcpy(&v, b + o, 4); return v; }

// CTXB layout (noclip oot3d/CTXB.ts):
//   0x00 'ctxb', 0x04 fileSize, 0x08 chunkCount, 0x10 texChunkOff, 0x14 texDataOff
//   texChunkOff -> "tex " chunk, byte-identical to a CMB tex chunk: magic@0, count@8,
//   entries@0x0C, each 0x24 bytes (data_len@0, width@8, height@0xA, fmt@0xC,
//   data_type@0xE, data_offset@0x10, name@0x14). Texels at texDataOff + entry.data_offset.
Ctxb::Ctxb(std::vector<uint8_t> data) : mData(std::move(data)) {
    const uint8_t* b = mData.data();
    if (mData.size() < 0x18 || memcmp(b, "ctxb", 4) != 0) { mErr = "not a ctxb"; return; }
    uint32_t texChunkOff = u32(b, 0x10);
    mTexDataOff = u32(b, 0x14);
    if (texChunkOff + 0x0C > mData.size() || memcmp(b + texChunkOff, "tex ", 4) != 0) {
        mErr = "missing tex chunk";
        return;
    }
    uint32_t n = u32(b, texChunkOff + 8);
    uint32_t o = texChunkOff + 0x0C;
    for (uint32_t i = 0; i < n; i++) {
        if (o + 0x24 > mData.size()) { mErr = "truncated tex chunk"; return; }
        CtxbTexture t;
        t.data_len = u32(b, o);
        t.width = u16(b, o + 8);
        t.height = u16(b, o + 0x0A);
        t.fmt = u16(b, o + 0x0C);
        t.data_type = u16(b, o + 0x0E);
        t.data_offset = u32(b, o + 0x10);
        size_t e = o + 0x14;
        while (e < o + 0x24 && b[e]) e++;
        t.name.assign((const char*)b + o + 0x14, e - (o + 0x14));
        mTextures.push_back(std::move(t));
        o += 0x24;
    }
    mOk = true;
}

std::vector<uint8_t> Ctxb::textureRaw(const CtxbTexture& t) const {
    size_t base = mTexDataOff + t.data_offset;
    if (base + t.data_len > mData.size()) return {};
    return std::vector<uint8_t>(mData.begin() + base, mData.begin() + base + t.data_len);
}

std::vector<uint8_t> Ctxb::decodeRGBA(size_t i, int* w, int* h) const {
    if (i >= mTextures.size()) return {};
    const CtxbTexture& t = mTextures[i];
    if (w) *w = t.width;
    if (h) *h = t.height;
    return PicaDecode(t.glFormat(), t.width, t.height, textureRaw(t));
}

} // namespace Zelda3D
