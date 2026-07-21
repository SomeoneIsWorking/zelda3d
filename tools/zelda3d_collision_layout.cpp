// MM3D scene-collision LAYOUT derivation aid.
//
// The shared asset/zcol parser (OoT3D layout) produces garbage on MM3D — see
// tools/zelda3d_collision_test.cpp, which is red on purpose. This tool exists to derive the MM3D
// layout from GROUND TRUTH rather than by nudging offsets until something looks plausible:
//
//   1. The ZSI header names the collision command's offset itself (command 0x03), so the header's
//      location is read from the format, never guessed.
//   2. Candidate field offsets are then judged by the format's OWN invariants — for a real
//      CollisionPoly, n . vA == -dist and the stored normal equals the geometric face normal of
//      (vA, vB, vC). Across thousands of polys those cannot hold by chance, so a candidate that
//      satisfies them IS the layout. This is precisely how the OoT3D layout in zcol.h was
//      established ("verified across 6 scenes: the plane identity holds ~100%").
//
// It prints the header words and the OoT3D-relative interpretation so the difference is visible,
// and dumps the same region for an OoT3D scene as a side-by-side reference.
//
// Build: tools/build_asset_test.sh
// Run:   scratch/bin/collision_layout [mmScene] [ootScene]
#include "asset/ctr_rom.h"
#include "asset/lzs.h"
#include "asset/zsi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

using namespace Zelda3D;

namespace {

uint16_t u16(const std::vector<uint8_t>& b, size_t o) {
    return (o + 2 <= b.size()) ? (uint16_t)(b[o] | (b[o + 1] << 8)) : 0;
}
uint32_t u32(const std::vector<uint8_t>& b, size_t o) {
    return (o + 4 <= b.size()) ? (uint32_t)(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24))
                               : 0;
}

std::vector<uint8_t> load(const char* romEnv, const std::string& path) {
    const char* rom = getenv(romEnv);
    if (!rom || !*rom) { printf("set %s\n", romEnv); return {}; }
    CtrRom r(rom);
    if (!r.ok()) { printf("CtrRom: %s\n", r.error().c_str()); return {}; }
    auto bytes = r.read(path);
    if (bytes.empty()) { printf("not found: %s\n", path.c_str()); return {}; }
    if (LzsIsCompressed(bytes)) {
        std::string e;
        auto inf = LzsDecompress(bytes, &e);
        if (inf.empty()) { printf("LzS: %s\n", e.c_str()); return {}; }
        bytes = std::move(inf);
    }
    return bytes;
}

void sweepPolyAnchor(const std::vector<uint8_t>& b, uint32_t hdr, bool mm3d);

void dumpCollision(const char* label, const std::vector<uint8_t>& b) {
    printf("=== %s (%zu bytes) ===\n", label, b.size());
    if (b.empty()) return;
    Zsi z(b);
    if (!z.ok()) { printf("  Zsi: %s\n", z.error().c_str()); return; }

    uint32_t colOff = 0;
    for (const auto& c : z.commands()) {
        printf("  cmd=0x%02X count=%-3u off=%u\n", c.type, c.count, c.offset);
        if (c.type == 0x03) colOff = c.offset; // Collision
    }
    if (colOff == 0 || colOff >= b.size()) {
        printf("  no collision command (0x03) — collision may live elsewhere for this game\n");
        return;
    }

    printf("  collision header @ %u\n  raw words:", colOff);
    for (int i = 0; i < 16; i++) {
        if (i % 8 == 0) printf("\n    +0x%02X:", i * 4);
        printf(" %08X", u32(b, colOff + i * 4));
    }
    printf("\n");
    // OoT3D interpretation, for contrast (zcol.h): +0x1c nVtx, +0x1e nPoly, +0x28 vtxList,
    // +0x2c polyList, +0x30 surfaceTypeList, +0x34 camData, +0x38 waterBox.
    printf("  OoT3D-layout reading: nVtx=%u nPoly=%u vtxList=%u polyList=%u surfList=%u cam=%u water=%u\n",
           u16(b, colOff + 0x1c), u16(b, colOff + 0x1e), u32(b, colOff + 0x28), u32(b, colOff + 0x2c),
           u32(b, colOff + 0x30), u32(b, colOff + 0x34), u32(b, colOff + 0x38));
    // Also print the s16 pairs across the header: a real bbox (minX,minY,minZ,maxX,maxY,maxZ) is
    // recognisable by min <= max on all three axes, which locates the header's start unambiguously.
    sweepPolyAnchor(b, colOff, b[3] == 0x09);
    printf("  s16 view:");
    for (int i = 0; i < 24; i++) {
        if (i % 12 == 0) printf("\n    +0x%02X:", i * 2);
        printf(" %6d", (int16_t)u16(b, colOff + i * 2));
    }
    printf("\n");
}

// Poly-record ANCHOR/field sweep, judged by the format's own plane invariant. The vertex array and
// counts are already pinned (the decoded vertex bbox reproduces the header's stored bbox exactly), so
// what remains is where each 20-byte CollisionPoly starts and where its normal/dist sit. Scoring by
// `n . vA == -dist` over hundreds of polys makes this a derivation, not a guess: a wrong anchor scores
// near 0 and the right one near 100, with no middle ground.
void sweepPolyAnchor(const std::vector<uint8_t>& b, uint32_t hdr, bool mm3d) {
    const size_t cnt = hdr + (mm3d ? 0x1e : 0x1c);
    const uint16_t nVtx = u16(b, cnt);
    const uint16_t nPoly = u16(b, cnt + 2);
    const uint32_t pVtx = u32(b, hdr + 0x28);
    const uint32_t pPoly = u32(b, hdr + 0x2c);
    const size_t vbase = (size_t)pVtx + 0x10;
    printf("  sweep: nVtx=%u nPoly=%u vbase=%zu pPoly=%u\n", nVtx, nPoly, vbase, pPoly);

    struct Cand { int anchor; int oNrm; int oDist; };
    // OoT3D field layout is (vA,vB,vC,flags, nx,ny,nz, dist): normal at +8, dist at +0xE. Also try
    // the shapes that a 2-byte insertion or a reordered flags field would produce.
    const int normOffs[] = { 8, 6, 10, 12 };
    const int distOffs[] = { 0xE, 0xC, 0x10, 8 };
    for (int anchor = -8; anchor <= 8; anchor += 2) {
        for (int oNrm : normOffs) {
            for (int oDist : distOffs) {
                if (oDist == oNrm) continue;
                const size_t pbase = (size_t)((long)pPoly + anchor);
                if (pbase + (size_t)nPoly * 20 > b.size()) continue;
                size_t hits = 0, checked = 0;
                for (uint16_t k = 0; k < nPoly; k++) {
                    const size_t o = pbase + (size_t)k * 20;
                    const uint16_t vA = u16(b, o) & 0x1FFF;
                    if (vA >= nVtx) continue;
                    const size_t vo = vbase + (size_t)vA * 6;
                    if (vo + 6 > b.size()) continue;
                    const double x = (int16_t)u16(b, vo), y = (int16_t)u16(b, vo + 2),
                                 z = (int16_t)u16(b, vo + 4);
                    const double nx = (int16_t)u16(b, o + oNrm) / 32767.0;
                    const double ny = (int16_t)u16(b, o + oNrm + 2) / 32767.0;
                    const double nz = (int16_t)u16(b, o + oNrm + 4) / 32767.0;
                    if (nx * nx + ny * ny + nz * nz < 0.25) continue;
                    float dist;
                    const uint32_t raw = u32(b, o + oDist);
                    memcpy(&dist, &raw, 4);
                    checked++;
                    if (std::fabs(nx * x + ny * y + nz * z + dist) <= 2.0) hits++;
                }
                if (checked >= 32 && hits * 100 / checked >= 90) {
                    printf("    HIT anchor=%+d normal=+0x%X dist=+0x%X -> plane %zu%% (%zu/%zu)\n",
                           anchor, oNrm, oDist, hits * 100 / checked, hits, checked);
                }
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string mmScene = (argc > 1) ? argv[1] : "z2_clocktower";
    const std::string ootScene = (argc > 2) ? argv[2] : "ydan";
    dumpCollision("OoT3D (reference, known-good layout)",
                  load("ZELDA3D_OOT3D_ROM", "/scene/" + ootScene + "_info.zsi"));
    dumpCollision("MM3D (under test)", load("ZELDA3D_MM3D_ROM", "/scenes/" + mmScene + "_info.zsi"));
    return 0;
}
