#include "zcol.h"
#include <cstring>

namespace Zelda3D {

static uint16_t u16le(const uint8_t* b, size_t o) {
    return (uint16_t)(b[o] | (b[o + 1] << 8));
}
static uint32_t u32le(const uint8_t* b, size_t o) {
    return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}
static int16_t s16le(const uint8_t* b, size_t o) {
    return (int16_t)u16le(b, o);
}
static float f32le(const uint8_t* b, size_t o) {
    uint32_t v = u32le(b, o);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static const uint8_t CMD_END = 0x14;
static const uint8_t CMD_COLLISION = 0x03;

OoT3DCollision::OoT3DCollision(const std::vector<uint8_t>& data) {
    const uint8_t* d = data.data();
    size_t n = data.size();
    if (n < 16 || (memcmp(d, "ZSI\x01", 4) != 0 && memcmp(d, "ZSI\x09", 4) != 0)) {
        mErr = "bad ZSI magic";
        return;
    }
    // Command list from 0x10; command addresses are plain file offsets.
    size_t hdr = 0;
    bool found = false;
    for (size_t off = 0x10; off + 8 <= n;) {
        uint8_t ctype = d[off]; // cmd1 is big-endian -> high byte = type
        uint32_t cmd2 = u32le(d, off + 4);
        if (ctype == CMD_COLLISION) {
            hdr = cmd2;
            found = true;
            break;
        }
        off += 8;
        if (ctype == CMD_END) break;
    }
    if (!found || hdr + 0x3c > n) {
        mErr = "no collision command";
        return;
    }
    uint16_t nVtx = u16le(d, hdr + 0x1c);
    uint16_t nPoly = u16le(d, hdr + 0x1e);
    uint16_t nSurf = u16le(d, hdr + 0x20);
    uint32_t pVtx = u32le(d, hdr + 0x28);
    uint32_t pPoly = u32le(d, hdr + 0x2c);
    uint32_t pSurf = u32le(d, hdr + 0x30);
    size_t vbase = (size_t)pVtx + 0x10;
    size_t pbase = (size_t)pPoly - 2;
    size_t sbase = (size_t)pSurf + 0x10; // surfaceType list (8 bytes/entry), same +0x10 as vtx
    if (vbase + (size_t)nVtx * 6 > n || pbase + (size_t)nPoly * 20 > n ||
        sbase + (size_t)nSurf * 8 > n) {
        mErr = "collision arrays out of bounds";
        return;
    }
    mSurfaces.reserve(nSurf);
    for (uint16_t s = 0; s < nSurf; s++) {
        mSurfaces.push_back({ u32le(d, sbase + (size_t)s * 8), u32le(d, sbase + (size_t)s * 8 + 4) });
    }
    mVerts.reserve(nVtx);
    for (uint16_t i = 0; i < nVtx; i++) {
        size_t o = vbase + (size_t)i * 6;
        mVerts.push_back({ s16le(d, o), s16le(d, o + 2), s16le(d, o + 4) });
    }
    mPolys.reserve(nPoly);
    for (uint16_t k = 0; k < nPoly; k++) {
        size_t o = pbase + (size_t)k * 20;
        Poly p;
        p.vA = u16le(d, o) & 0x1fff;
        p.vB = u16le(d, o + 2) & 0x1fff;
        p.vC = u16le(d, o + 4) & 0x1fff;
        p.nx = s16le(d, o + 8);
        p.ny = s16le(d, o + 10);
        p.nz = s16le(d, o + 12);
        p.dist = f32le(d, o + 14);
        p.type = u16le(d, o + 18); // index into the surfaceType list
        if (p.type >= nSurf) p.type = 0;
        // Skip degenerate / out-of-range polys.
        if (p.vA >= nVtx || p.vB >= nVtx || p.vC >= nVtx) continue;
        mPolys.push_back(p);
    }
    mOk = true;
}

} // namespace Zelda3D
