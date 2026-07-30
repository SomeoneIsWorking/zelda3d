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
    // MM3D shifts the COUNT triplet 2 bytes later than OoT3D; the bbox that precedes it sits at
    // +0x12 instead of +0x10. The POINTERS at +0x28/+0x2C/+0x30 are NOT shifted. Discriminated by the
    // ZSI version in the magic (OoT3D "ZSI\x01" vs MM3D "ZSI\x09"), which is the format's own
    // version field rather than a guess about the game.
    //
    // Derived, not tuned — z2_clocktower's counts are pinned by the array arithmetic:
    //   verts: pVtx(616)  + 651*6  = 4522 == pPoly(4524) - 2      <- the same -2 anchor as OoT3D
    //   polys: 4522       + 731*20 = 19142 ~= pSurf(19144)
    //   surfs: pSurf+0x10 + 24*8   = 19352 <= waterBox(19360)
    // Reading OoT3D's +0x1c/+0x1e here instead yields nVtx=1120, whose vertex array would run past
    // the polygon array — and the plane/face-normal invariants collapse to 0.6%/2.2%.
    const bool mm3d = (d[3] == 0x09);
    const size_t cnt = hdr + (mm3d ? 0x1e : 0x1c);
    uint16_t nVtx = u16le(d, cnt);
uint16_t nPoly = u16le(d, cnt + 2);
    uint16_t nSurf = u16le(d, cnt + 4);
    uint32_t pVtx = u32le(d, hdr + 0x28);
    uint32_t pPoly = u32le(d, hdr + 0x2c);
    uint32_t pSurf = u32le(d, hdr + 0x30);
    size_t vbase = (size_t)pVtx + 0x10;
    // Polygon array anchor. CORRECTED 2026-07-30: it is pPoly + 18, not pPoly - 2 — the shipped
    // value was exactly ONE 20-byte record early, so the array was read shifted by one: a phantom
    // record 0 fabricated from vertex-array tail bytes, and the last real polygon of every scene
    // dropped.
    //
    // Brute-forced anchor x record-layout over ALL 114 scene headers / 170175 polygons, scoring on
    // vertex indices < nVtx AND |normal| ~= 32767 AND |n.vA + dist| <= 1.5:
    //     pPoly + 18 : 170175/170175 valid (100.000%)   index -1 valid in 0 scenes, index nPoly in 0
    //     pPoly -  2 : 170061/170175 valid ( 99.933%)   index -1 valid in 0 scenes, index nPoly in 114
    // At +18 the array is bounded EXACTLY on both sides — nothing valid before it, nothing after —
    // which is what an anchor being right looks like. Corroborated by exact list tiling in 114/114
    // scenes: vtxList+0x10 + 6*nVtx == polyList+0x10, polyList+0x10 + 20*nPoly ==
    // surfaceTypeList+0x10, surfaceTypeList+0x10 + 8*nSurf == camData+0x10.
    //
    // The stale -2 also produced the 107 out-of-bounds indices and 7 non-unit normals that the
    // bounds check at the loop below was silently absorbing; with the right anchor those are zero.
    // (An earlier attempt at this bug read nPoly+1 records from the OLD anchor. That recovered the
    // dropped polygon but KEPT the phantom record 0 — a superset, not a fix. Fixing the anchor is
    // the actual correction.)
    size_t pbase = (size_t)pPoly + 18;
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
        // MM3D's 20-byte CollisionPoly omits the 2-byte flags word OoT3D carries at +0x06, so its
        // normal starts at +0x06 instead of +0x08. Everything else is shared: same -2 array anchor,
        // same f32 plane distance at +0x0E, same s16/32767 normal encoding.
        //
        // Derived against the format's own plane identity (n . vA == -dist), swept over anchors and
        // field offsets by tools/zelda3d_collision_layout.cpp. The sweep recovers OoT3D's documented
        // layout (anchor -2, normal +0x8, dist +0xE -> 100%, 2844/2844) before being trusted on MM3D
        // (anchor -2, normal +0x6, dist +0xE -> 100%, 730/730). A wrong offset scores ~0%, not
        // "slightly worse", so this is a derivation rather than a fit.
        const int oNrm = mm3d ? 6 : 8;
        p.nx = s16le(d, o + oNrm);
        p.ny = s16le(d, o + oNrm + 2);
        p.nz = s16le(d, o + oNrm + 4);
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
