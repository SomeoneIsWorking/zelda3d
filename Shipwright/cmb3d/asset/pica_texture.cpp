#include "pica_texture.h"
#include <cstring>

namespace Zelda3D {

static inline uint8_t e4(uint8_t n) { return (n << 4) | n; }
static inline uint8_t e5(uint8_t n) { return (n << 3) | (n >> 2); }
static inline uint8_t e6(uint8_t n) { return (n << 2) | (n >> 4); }
static inline uint8_t clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }
static inline int morton7(int n) { return ((n >> 2) & 0x04) | ((n >> 1) & 0x02) | (n & 0x01); }
static inline int s3(int n) { return (n & 4) ? n - 8 : n; } // sign-extend 3-bit

static uint32_t rd32(const uint8_t* d, size_t o) { uint32_t v; memcpy(&v, d + o, 4); return v; }
static uint16_t rd16(const uint8_t* d, size_t o) { uint16_t v; memcpy(&v, d + o, 2); return v; }

// glFormat constants (dataType<<16 | formatConstant)
enum {
    GF_ETC1 = 0x0000675A, GF_ETC1A4 = 0x0000675B,
    GF_RGB8 = 0x14016754, GF_RGBA8 = 0x14016752,
    GF_RGBA4444 = 0x80336752, GF_RGBA5551 = 0x80346752, GF_RGB565 = 0x83636754,
    GF_A8 = 0x14016756, GF_A4 = 0x67616756,
    GF_L8 = 0x14016757, GF_L4 = 0x67616757,
    GF_LA8 = 0x14016758, GF_LA4 = 0x67606758,
    GF_HILO8 = 0x14016759,
};

static const int INTENS[8][4] = {
    { 2, 8, -2, -8 }, { 5, 17, -5, -17 }, { 9, 29, -9, -29 }, { 13, 42, -13, -42 },
    { 18, 60, -18, -60 }, { 24, 80, -24, -80 }, { 33, 106, -33, -106 }, { 47, 183, -47, -183 }
};

static void etc1Color(uint8_t* dst, uint32_t w1, uint32_t w2, size_t dstOffs, int stride) {
    bool diff = (w1 & 0x02) != 0;
    bool flip = (w1 & 0x01) != 0;
    const int* it1 = INTENS[(w1 >> 5) & 7];
    const int* it2 = INTENS[(w1 >> 2) & 7];
    uint8_t c1[4][3], c2[4][3];
    auto colors = [](int r, int g, int b, const int* it, uint8_t out[4][3]) {
        for (int i = 0; i < 4; i++) { out[i][0] = clamp8(r + it[i]); out[i][1] = clamp8(g + it[i]); out[i][2] = clamp8(b + it[i]); }
    };
    if (diff) {
        int r1a = (w1 >> 27) & 0x1F, r2d = s3((w1 >> 24) & 7);
        int g1a = (w1 >> 19) & 0x1F, g2d = s3((w1 >> 16) & 7);
        int b1a = (w1 >> 11) & 0x1F, b2d = s3((w1 >> 8) & 7);
        colors(e5(r1a), e5(g1a), e5(b1a), it1, c1);
        colors(e5(r1a + r2d), e5(g1a + g2d), e5(b1a + b2d), it2, c2);
    } else {
        colors(e4((w1 >> 28) & 0xF), e4((w1 >> 20) & 0xF), e4((w1 >> 12) & 0xF), it1, c1);
        colors(e4((w1 >> 24) & 0xF), e4((w1 >> 16) & 0xF), e4((w1 >> 8) & 0xF), it2, c2);
    }
    for (int i = 0; i < 16; i++) {
        int lsb = (w2 >> i) & 1, msb = (w2 >> (16 + i)) & 1, lookup = (msb << 1) | lsb;
        int y = i & 3, x = i >> 2;
        size_t di = dstOffs + ((size_t)(y * stride) + x) * 4;
        int which = flip ? (y & 2) : (x & 2);
        const uint8_t* col = which ? c2[lookup] : c1[lookup];
        dst[di] = col[0]; dst[di + 1] = col[1]; dst[di + 2] = col[2];
    }
}

static void etc1Alpha(uint8_t* dst, uint32_t a1, uint32_t a2, size_t dstOffs, int stride) {
    for (int ax = 0; ax < 2; ax++)
        for (int ay = 0; ay < 4; ay++) { dst[dstOffs + ((size_t)(ay * stride) + ax) * 4 + 3] = e4(a2 & 0xF); a2 >>= 4; }
    for (int ax = 2; ax < 4; ax++)
        for (int ay = 0; ay < 4; ay++) { dst[dstOffs + ((size_t)(ay * stride) + ax) * 4 + 3] = e4(a1 & 0xF); a1 >>= 4; }
}

static std::vector<uint8_t> decodeEtc1(int width, int height, const std::vector<uint8_t>& data, bool alpha) {
    std::vector<uint8_t> px((size_t)width * height * 4, 0);
    const uint8_t* d = data.data();
    size_t need = data.size();
    int stride = width;
    size_t offs = 0;
    for (int yy = 0; yy < height; yy += 8)
        for (int xx = 0; xx < width; xx += 8)
            for (int y = 0; y < 8; y += 4)
                for (int x = 0; x < 8; x += 4) {
                    size_t dstOffs = ((size_t)(yy + y) * stride + (xx + x)) * 4;
                    uint32_t a1 = 0xFFFFFFFF, a2 = 0xFFFFFFFF;
                    if (alpha) {
                        if (offs + 8 > need) return px;
                        a2 = rd32(d, offs); a1 = rd32(d, offs + 4); offs += 8;
                    }
                    etc1Alpha(px.data(), a1, a2, dstOffs, stride);
                    if (offs + 8 > need) return px;
                    uint32_t w2 = rd32(d, offs), w1 = rd32(d, offs + 4); offs += 8;
                    etc1Color(px.data(), w1, w2, dstOffs, stride);
                }
    return px;
}

// Tiled (8x8 Morton swizzle) decode driven by a per-texel sampler.
template <typename F>
static std::vector<uint8_t> tiled(int width, int height, F sample) {
    std::vector<uint8_t> px((size_t)width * height * 4, 0);
    int si = 0;
    for (int yy = 0; yy < height; yy += 8)
        for (int xx = 0; xx < width; xx += 8)
            for (int i = 0; i < 0x40; i++) {
                int x = morton7(i), y = morton7(i >> 1);
                size_t d = ((size_t)(yy + y) * width + (xx + x)) * 4;
                uint8_t r, g, b, a;
                sample(si++, r, g, b, a);
                px[d] = r; px[d + 1] = g; px[d + 2] = b; px[d + 3] = a;
            }
    return px;
}

std::vector<uint8_t> PicaDecode(uint32_t glFormat, int width, int height, const std::vector<uint8_t>& data) {
    const uint8_t* d = data.data();
    size_t n = data.size();
    switch (glFormat) {
        case GF_ETC1: return decodeEtc1(width, height, data, false);
        case GF_ETC1A4: return decodeEtc1(width, height, data, true);
        case GF_RGBA8:
            if (n < (size_t)width * height * 4) return {};
            return tiled(width, height, [&](int i, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
                uint32_t p = rd32(d, (size_t)i * 4); r = (p >> 24) & 0xFF; g = (p >> 16) & 0xFF; b = (p >> 8) & 0xFF; a = p & 0xFF;
            });
        case GF_RGB8:
            if (n < (size_t)width * height * 3) return {};
            return tiled(width, height, [&](int i, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
                r = d[i * 3 + 2]; g = d[i * 3 + 1]; b = d[i * 3 + 0]; a = 0xFF;
            });
        case GF_RGB565:
            return tiled(width, height, [&](int i, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
                uint16_t p = rd16(d, (size_t)i * 2); r = e5((p >> 11) & 0x1F); g = e6((p >> 5) & 0x3F); b = e5(p & 0x1F); a = 0xFF;
            });
        case GF_RGBA5551:
            return tiled(width, height, [&](int i, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
                uint16_t p = rd16(d, (size_t)i * 2); r = e5((p >> 11) & 0x1F); g = e5((p >> 6) & 0x1F); b = e5((p >> 1) & 0x1F); a = (p & 1) ? 0xFF : 0;
            });
        case GF_RGBA4444:
            return tiled(width, height, [&](int i, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
                uint16_t p = rd16(d, (size_t)i * 2); r = e4((p >> 12) & 0xF); g = e4((p >> 8) & 0xF); b = e4((p >> 4) & 0xF); a = e4(p & 0xF);
            });
        case GF_LA8:
            return tiled(width, height, [&](int i, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
                a = d[i * 2]; uint8_t L = d[i * 2 + 1]; r = g = b = L;
            });
        case GF_HILO8:
            return tiled(width, height, [&](int i, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
                r = d[i * 2 + 1]; g = d[i * 2]; b = 0xFF; a = 0xFF;
            });
        case GF_L8:
            // L8 (PICA200 "Luminance") carries ONLY a luminance channel -- there is no stored
            // alpha data, so the hardware convention (matching GL_LUMINANCE / PicaLegacyHashBytes'
            // own "I8 -> {L,L,L,255}" correction just below) is alpha = fully OPAQUE. Previously
            // this set a = L, aliasing luminance into alpha; any additive-blend material that
            // reads texture alpha as its SRC_ALPHA blend factor (e.g. fine_star: blend_src_rgb=
            // GL_SRC_ALPHA, blend_dst_rgb=GL_ONE) then squared its own brightness (contribution =
            // L * L/255 instead of L), crushing bright texels toward black. Root cause of the
            // title-screen star-brightness deficit (SoH capped ~70/255 vs oracle >140/255).
            return tiled(width, height, [&](int i, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
                uint8_t L = d[i]; r = g = b = L; a = 0xFF;
            });
        case GF_A8:
            return tiled(width, height, [&](int i, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
                r = g = b = 0xFF; a = d[i];
            });
        case GF_L4:
            // Same luminance-only convention as GF_L8 above -- alpha is opaque, not aliased to L.
            return tiled(width, height, [&](int i, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
                uint8_t p = d[i >> 1]; uint8_t nn = (i & 1) ? (p >> 4) : (p & 0xF); uint8_t L = e4(nn); r = g = b = L; a = 0xFF;
            });
        case GF_A4:
            return tiled(width, height, [&](int i, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
                uint8_t p = d[i >> 1]; uint8_t nn = (i & 1) ? (p >> 4) : (p & 0xF); r = g = b = 0xFF; a = e4(nn);
            });
        default: return {};
    }
}

// Native bytes-per-pixel for the formats Citra copies verbatim (converted=false). Returns 0
// for formats Citra always decodes to RGBA8.
static int legacyNativeBpp(uint32_t glFormat) {
    switch (glFormat) {
        case GF_RGBA8: return 4;
        case GF_RGB8: return 3;
        case GF_RGB565:
        case GF_RGBA5551:
        case GF_RGBA4444: return 2;
        default: return 0; // ETC1/ETC1A4/IA8/I8/A8/IA4/I4/A4/... -> RGBA8
    }
}

std::vector<uint8_t> PicaLegacyHashBytes(uint32_t glFormat, int width, int height,
                                         const std::vector<uint8_t>& data) {
    int nbpp = legacyNativeBpp(glFormat);
    if (nbpp == 0) {
        // RGBA8-output formats: Citra's decoded == our RGBA8 decode, but stored bottom-up.
        auto rgba = PicaDecode(glFormat, width, height, data);
        if (rgba.empty()) return {};
        // A8/A4 differ from our renderer's decode in the unused RGB channels; match Citra's
        // exact convention so the hash lines up (Common::Color): A8/A4 -> {0,0,0,A}. (L8/L4
        // already decode as {L,L,L,255} in PicaDecode itself now — see its GF_L8/GF_L4 cases.)
        if (glFormat == GF_A8 || glFormat == GF_A4) {
            for (size_t i = 0; i + 3 < rgba.size(); i += 4) rgba[i] = rgba[i + 1] = rgba[i + 2] = 0;
        }
        std::vector<uint8_t> out(rgba.size());
        size_t row = (size_t)width * 4;
        for (int y = 0; y < height; y++)
            memcpy(out.data() + (size_t)y * row, rgba.data() + (size_t)(height - 1 - y) * row, row);
        return out;
    }
    // Native-copy color formats: morton-detile + vertical flip, keeping native bytes. The
    // raw stream is laid out in morton-traversal order within each 8x8 tile (same order our
    // tiled() sampler reads), so copy nbpp bytes per texel into the flipped linear position.
    if (data.size() < (size_t)width * height * nbpp) return {};
    std::vector<uint8_t> out((size_t)width * height * nbpp);
    const uint8_t* d = data.data();
    int si = 0;
    for (int yy = 0; yy < height; yy += 8)
        for (int xx = 0; xx < width; xx += 8)
            for (int i = 0; i < 0x40; i++) {
                int x = morton7(i), y = morton7(i >> 1);
                int ly = height - 1 - (yy + y);
                size_t dst = ((size_t)ly * width + (xx + x)) * nbpp;
                memcpy(out.data() + dst, d + (size_t)si * nbpp, nbpp);
                si++;
            }
    return out;
}

} // namespace Zelda3D
