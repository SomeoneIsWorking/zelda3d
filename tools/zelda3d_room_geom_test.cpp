// Room-geometry well-formedness check for scene-room CMBs (OoT3D and MM3D).
//
// WHY: the MM3D scene-room port renders FRAGMENTED/mispositioned geometry while the OoT3D one is
// correct. Both go through the same Zsi -> Cmb -> buildDrawGroups path, so this compares the two
// games' room geometry structurally, with the OoT3D room as the known-good reference.
//
// PRIMARY ASSERTION: every room vertex position must be FINITE. Written first as a spread metric,
// which wrongly "passed" the MM3D room because its bbox came out as inf -- i.e. the decoded positions
// contain inf/NaN. That non-finiteness IS the bug (garbage positions scatter the geometry), so it is
// the assertion that matters; spread is kept as secondary reporting only.
//
// Build: tools/build_asset_test.sh
// Run:   ZELDA3D_OOT3D_ROM=... ZELDA3D_MM3D_ROM=... scratch/bin/room_geom_test
#include "asset/cmb.h"
#include "asset/ctr_rom.h"
#include "asset/lzs.h"
#include "asset/zsi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace Zelda3D;

namespace {

// No Zelda scene room is anywhere near this large; OoT3D's ydan_0 spans ~2.7e3 units. Anything past
// this is a decode error, not level design.
constexpr double kSaneExtent = 1.0e6;

struct RoomStats {
    bool ok = false;
    std::string err;
    size_t groups = 0, verts = 0;
    float roomDiag = 0.0f;    // diagonal of the bbox over ALL groups
    float medGroupDiag = 0.0f; // median diagonal of an individual group's bbox
    float spread = 0.0f;       // roomDiag / medGroupDiag
    int distinctCentroids = 0; // group centroids further apart than 1 unit
    size_t nonFinite = 0;      // vertex components that are inf/NaN -- MUST be 0
    size_t insane = 0;         // |component| beyond any plausible scene extent
    double maxAbs = 0.0;       // largest |component| seen
    float sampleBad[3] = { 0, 0, 0 };
};

float diag(const float lo[3], const float hi[3]) {
    if (hi[0] < lo[0]) return 0.0f;
    float d = 0.0f;
    for (int k = 0; k < 3; k++) {
        const float e = hi[k] - lo[k];
        d += e * e;
    }
    return std::sqrt(d);
}

RoomStats analyze(const char* romEnv, const char* path) {
    RoomStats s;
    const char* rom = getenv(romEnv);
    if (!rom || !*rom) { s.err = std::string("set ") + romEnv; return s; }
    CtrRom r(rom);
    if (!r.ok()) { s.err = "CtrRom: " + r.error(); return s; }
    auto bytes = r.read(path);
    if (bytes.empty()) { s.err = std::string("not found: ") + path; return s; }
    // MM3D stores its scene ZSIs LzS-compressed; OoT3D stores them raw.
    if (LzsIsCompressed(bytes)) {
        std::string e;
        auto inflated = LzsDecompress(bytes, &e);
        if (inflated.empty()) { s.err = "LzS: " + e; return s; }
        bytes = std::move(inflated);
    }
    Zsi z(std::move(bytes));
    if (!z.ok()) { s.err = "Zsi: " + z.error(); return s; }
    if (!z.hasGeometry()) { s.err = "no room geometry"; return s; }
    Cmb c(z.cmbBytes());
    if (!c.ok()) { s.err = "Cmb: " + c.error(); return s; }

    auto groups = c.buildDrawGroups();
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    std::vector<float> gdiag;
    std::vector<std::array<float, 3>> centroids;
    for (const auto& g : groups) {
        if (g.verts.empty()) continue;
        float glo[3] = { 1e30f, 1e30f, 1e30f }, ghi[3] = { -1e30f, -1e30f, -1e30f };
        double acc[3] = { 0, 0, 0 };
        for (const auto& v : g.verts) {
            if (!std::isfinite(v.pos[0]) || !std::isfinite(v.pos[1]) || !std::isfinite(v.pos[2])) {
                if (s.nonFinite == 0) {
                    s.sampleBad[0] = v.pos[0]; s.sampleBad[1] = v.pos[1]; s.sampleBad[2] = v.pos[2];
                }
                s.nonFinite++;
                continue; // don't poison the bbox
            }
            for (int k = 0; k < 3; k++) {
                const double a = std::fabs((double)v.pos[k]);
                if (a > s.maxAbs) s.maxAbs = a;
                if (a > kSaneExtent) {
                    if (s.insane == 0) {
                        s.sampleBad[0] = v.pos[0]; s.sampleBad[1] = v.pos[1]; s.sampleBad[2] = v.pos[2];
                    }
                    s.insane++;
                }
                glo[k] = std::min(glo[k], v.pos[k]);
                ghi[k] = std::max(ghi[k], v.pos[k]);
                lo[k] = std::min(lo[k], v.pos[k]);
                hi[k] = std::max(hi[k], v.pos[k]);
                acc[k] += v.pos[k];
            }
        }
        s.verts += g.verts.size();
        gdiag.push_back(diag(glo, ghi));
        centroids.push_back({ (float)(acc[0] / g.verts.size()), (float)(acc[1] / g.verts.size()),
                              (float)(acc[2] / g.verts.size()) });
    }
    s.groups = gdiag.size();
    if (gdiag.empty()) { s.err = "no non-empty draw groups"; return s; }
    std::sort(gdiag.begin(), gdiag.end());
    s.medGroupDiag = gdiag[gdiag.size() / 2];
    s.roomDiag = diag(lo, hi);
    s.spread = (s.medGroupDiag > 0.0f) ? s.roomDiag / s.medGroupDiag : 0.0f;

    for (size_t i = 0; i < centroids.size(); i++) {
        bool uniq = true;
        for (size_t j = 0; j < i; j++) {
            const float dx = centroids[i][0] - centroids[j][0];
            const float dy = centroids[i][1] - centroids[j][1];
            const float dz = centroids[i][2] - centroids[j][2];
            if (std::sqrt(dx * dx + dy * dy + dz * dz) < 1.0f) { uniq = false; break; }
        }
        if (uniq) s.distinctCentroids++;
    }
    s.ok = true;
    return s;
}

void report(const char* label, const RoomStats& s) {
    if (!s.ok) { printf("%-28s FAILED: %s\n", label, s.err.c_str()); return; }
    printf("%-28s groups=%3zu verts=%6zu roomDiag=%9.1f medGroupDiag=%8.1f spread=%6.2f distinct=%d nonFinite=%zu insane=%zu maxAbs=%.3g\n",
           label, s.groups, s.verts, s.roomDiag, s.medGroupDiag, s.spread, s.distinctCentroids, s.nonFinite, s.insane, s.maxAbs);
    if (s.nonFinite || s.insane) {
        printf("%-28s   first bad vertex: (%g, %g, %g)\n", "", s.sampleBad[0], s.sampleBad[1], s.sampleBad[2]);
    }
}

} // namespace

int main(int argc, char** argv) {
    const char* ootPath = (argc > 1) ? argv[1] : "/scene/ydan_0_info.zsi";
    const char* mmPath = (argc > 2) ? argv[2] : "/scenes/z2_clocktower_0_info.zsi";

    RoomStats oot = analyze("ZELDA3D_OOT3D_ROM", ootPath);
    RoomStats mm = analyze("ZELDA3D_MM3D_ROM", mmPath);
    printf("REFERENCE (OoT3D, renders correctly):\n  ");
    report(ootPath, oot);
    printf("UNDER TEST (MM3D):\n  ");
    report(mmPath, mm);

    if (!oot.ok || !mm.ok) {
        printf("\nRESULT: INCONCLUSIVE (a room failed to load)\n");
        return 2;
    }
    // PRIMARY: decoded vertex positions must be finite in BOTH games.
    if (oot.nonFinite != 0) {
        printf("\nRESULT: FAIL — the OoT3D reference room itself has %zu non-finite positions.\n", oot.nonFinite);
        return 1;
    }
    if (mm.insane != 0 || oot.insane != 0) {
        printf("\nRESULT: FAIL — %zu MM3D room vertex components exceed any plausible scene extent "
               "(max |component| = %.3g; OoT3D reference max = %.3g, insane=%zu).\n"
               "        Decoded room positions are garbage, which is what scatters the geometry.\n",
               mm.insane, mm.maxAbs, oot.maxAbs, oot.insane);
        return 1;
    }
    if (mm.nonFinite != 0) {
        printf("\nRESULT: FAIL — MM3D room has %zu non-finite vertex positions (OoT3D reference: 0).\n"
               "        Decoded room positions are garbage, which is what scatters the geometry.\n",
               mm.nonFinite);
        return 1;
    }
    // SECONDARY: placement spread should resemble the known-good reference.
    const float bar = oot.spread * 0.5f;
    printf("\nbar = 0.5 * OoT3D spread = %.2f ; MM3D spread = %.2f\n", bar, mm.spread);
    if (mm.spread < bar) {
        printf("RESULT: FAIL — MM3D room geometry is not spread like a real scene room.\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
