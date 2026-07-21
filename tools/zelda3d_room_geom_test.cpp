// Room-geometry well-formedness check for scene-room CMBs (OoT3D and MM3D).
//
// WHY: the MM3D scene-room port renders FRAGMENTED/mispositioned geometry while the OoT3D one is
// correct. Both go through the same Zsi -> Cmb -> buildDrawGroups path, so this compares the two
// games' room geometry structurally, with the OoT3D room as the known-good reference.
//
// RECONCILED (measured): excluding the bad components, the MM3D room's extent is HEALTHY
// (roomDiag 6493 vs OoT3D 2707, spread 1.51 vs 2.05) — Clock Town is simply a bigger room. So the
// room geometry PARSES CORRECTLY and the defect is narrow: a handful of vertices whose index
// overruns their attribute buffer. Those land at ~1e38, which stretches a few triangles across the
// whole screen — which is what read as "fragmented/inverted" geometry in-game. The magnitude check
// below is therefore testing the real visible bug, not a cosmetic outlier.
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
#include <map>
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
    size_t orphanSepds = 0;
    std::map<uint16_t, int> idxHist;
    std::string rangeReport;
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

    // ZSI command inventory + actor list. MM3D room geometry is drawn DISPLACED relative to Link
    // (who is in N64 coordinates), so the scene-space -> world-space convention has to be derived.
    // A room's actor entries are the correspondence to do it with: the same actors exist in the N64
    // scene, so pairs of positions determine the transform (and let the residual be checked) instead
    // of an offset being tuned by eye. ZELDA3D_ZSI_CMDS=1.
    if (getenv("ZELDA3D_ZSI_CMDS")) {
        const auto& raw = z.raw();
        for (const auto& cmd : z.commands()) {
            printf("      [zsi] cmd=0x%02X count=%-3u off=%u\n", cmd.type, cmd.count, cmd.offset);
            if (cmd.type != 0x01 || cmd.count == 0) continue; // 0x01 = actor list
            for (unsigned i = 0; i < cmd.count && i < 12; i++) {
                const size_t o = (size_t)cmd.offset + (size_t)i * 16; // N64-shaped entry: id,xyz,rot,params
                if (o + 16 > raw.size()) break;
                auto s16at = [&](size_t k) { return (int16_t)(raw[o + k] | (raw[o + k + 1] << 8)); };
                printf("        actor[%2u] id=0x%04X pos=(%6d,%6d,%6d) rot=(%6d,%6d,%6d) params=0x%04X\n",
                       i, (uint16_t)s16at(0), s16at(2), s16at(4), s16at(6), s16at(8), s16at(10),
                       s16at(12), (uint16_t)s16at(14));
            }
        }
    }
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
                    continue; // keep the CLEAN extent measurable
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
    // Per-group bbox dump: a scene floor is a WIDE, FLAT, LOW group. If one exists here but not on
    // screen, the defect is at draw (material/cull/alpha), not in parsing. Set ZELDA3D_GROUP_DUMP=1.
    if (getenv("ZELDA3D_GROUP_DUMP")) {
        for (size_t gi = 0; gi < groups.size(); gi++) {
            const auto& g = groups[gi];
            if (g.verts.empty()) continue;
            float a[3] = { 1e30f, 1e30f, 1e30f }, b2[3] = { -1e30f, -1e30f, -1e30f };
            for (const auto& v : g.verts)
                for (int k = 0; k < 3; k++) {
                    if (!std::isfinite(v.pos[k])) continue;
                    a[k] = std::min(a[k], v.pos[k]);
                    b2[k] = std::max(b2[k], v.pos[k]);
                }
            const float ex = b2[0] - a[0], ey = b2[1] - a[1], ez = b2[2] - a[2];
            const float flat = (ey > 1e-3f) ? std::max(ex, ez) / ey : 1e9f;
            printf("      grp%-3zu verts=%5zu  x[%8.1f %8.1f] y[%8.1f %8.1f] z[%8.1f %8.1f]  ext=%.0fx%.0fx%.0f flat=%.1f%s\n",
                   gi, g.verts.size(), a[0], b2[0], a[1], b2[1], a[2], b2[2], ex, ey, ez, flat,
                   (flat > 8.0f && std::max(ex, ez) > 300.0f) ? "  <-- FLOOR-LIKE" : "");
        }
    }
    // GROUND PROBE: ray-cast straight down (and up) through the room mesh at a given XZ and report
    // every horizontal surface height there. This answers the placement question numerically at the
    // exact spot the player stands, instead of inferring a displacement from screenshots.
    //   ZELDA3D_GROUND_AT="x,z"   (e.g. Link's live N64 position)
    if (const char* q = getenv("ZELDA3D_GROUND_AT")) {
        double qx = 0, qz = 0;
        if (sscanf(q, "%lf,%lf", &qx, &qz) == 2) {
            std::vector<float> hits;
            for (const auto& g : groups) {
                for (size_t i = 0; i + 2 < g.verts.size(); i += 3) {
                    const float* p0 = g.verts[i].pos;
                    const float* p1 = g.verts[i + 1].pos;
                    const float* p2 = g.verts[i + 2].pos;
                    // barycentric containment in the XZ plane
                    const double d = (p1[2] - p2[2]) * (p0[0] - p2[0]) + (p2[0] - p1[0]) * (p0[2] - p2[2]);
                    if (std::fabs(d) < 1e-9) continue;
                    const double a = ((p1[2] - p2[2]) * (qx - p2[0]) + (p2[0] - p1[0]) * (qz - p2[2])) / d;
                    const double bb = ((p2[2] - p0[2]) * (qx - p2[0]) + (p0[0] - p2[0]) * (qz - p2[2])) / d;
                    const double cc = 1.0 - a - bb;
                    if (a < 0 || bb < 0 || cc < 0) continue;
                    const double y = a * p0[1] + bb * p1[1] + cc * p2[1];
                    if (std::isfinite(y)) hits.push_back((float)y);
                }
            }
            std::sort(hits.begin(), hits.end());
            printf("      [ground] at XZ=(%.1f, %.1f): %zu surface(s):", qx, qz, hits.size());
            for (size_t i = 0; i < hits.size() && i < 10; i++) printf(" %.1f", hits[i]);
            printf("\n");
        }
    }
    // FLOOR CENSUS: does the room actually contain horizontal, ground-height triangles? A missing
    // floor on screen has two very different causes -- drawn-but-invisible (material/cull/depth) vs
    // simply not in this file (MM3D storing terrain elsewhere). Counting flat low triangles tells
    // them apart without touching the renderer. ZELDA3D_FLOOR_CENSUS=1.
    if (getenv("ZELDA3D_FLOOR_CENSUS")) {
        double floorArea = 0.0;
        size_t floorTris = 0, totalTris = 0;
        float fy = 0.0f;
        for (const auto& g : groups) {
            for (size_t i = 0; i + 2 < g.verts.size(); i += 3) {
                const float* p0 = g.verts[i].pos;
                const float* p1 = g.verts[i + 1].pos;
                const float* p2 = g.verts[i + 2].pos;
                bool fin = true;
                for (int k = 0; k < 3; k++)
                    fin = fin && std::isfinite(p0[k]) && std::isfinite(p1[k]) && std::isfinite(p2[k]);
                if (!fin) continue;
                totalTris++;
                const float u[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
                const float v[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
                const float nx = u[1] * v[2] - u[2] * v[1];
                const float ny = u[2] * v[0] - u[0] * v[2];
                const float nz = u[0] * v[1] - u[1] * v[0];
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len <= 1e-6f) continue;
                // |normal.y| dominant => horizontal surface; low y => ground height, not a ceiling.
                const float upness = std::fabs(ny) / len;
                const float ymax = std::max(p0[1], std::max(p1[1], p2[1]));
                if (upness > 0.85f && ymax < 120.0f) {
                    floorTris++;
                    floorArea += 0.5 * len;
                    fy += ymax;
                }
            }
        }
        printf("      [floor] horizontal low tris=%zu / %zu   area=%.3g sq units   meanY=%.1f\n",
               floorTris, totalTris, floorArea, floorTris ? fy / floorTris : 0.0f);
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
    s.orphanSepds = c.unreferencedSepdCount();
    s.idxHist = c.indexTypeHistogram();
    s.rangeReport = c.indexRangeReport();
    s.ok = true;
    return s;
}

void report(const char* label, const RoomStats& s) {
    if (!s.ok) { printf("%-28s FAILED: %s\n", label, s.err.c_str()); return; }
    printf("%-28s groups=%3zu verts=%6zu roomDiag=%9.1f medGroupDiag=%8.1f spread=%6.2f distinct=%d nonFinite=%zu insane=%zu maxAbs=%.3g\n",
           label, s.groups, s.verts, s.roomDiag, s.medGroupDiag, s.spread, s.distinctCentroids, s.nonFinite, s.insane, s.maxAbs);
    printf("%-28s   orphanSepds=%zu\n", "", s.orphanSepds);
    if (s.nonFinite || s.insane) {
        printf("%-28s   first bad vertex: (%g, %g, %g)\n", "", s.sampleBad[0], s.sampleBad[1], s.sampleBad[2]);
    }
    if (!s.idxHist.empty()) {
        printf("%-28s   prm.index_type:", "");
        for (const auto& kv : s.idxHist) printf(" 0x%X x%d", kv.first, kv.second);
        printf("\n");
    }
    if (!s.rangeReport.empty()) printf("%s", s.rangeReport.c_str());
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
    // SECONDARY: every sepd must be reachable from a mesh entry.
    //
    // This REPLACES an earlier "spread resembles the reference room" heuristic, which was a bad
    // discriminator: it assumed a room is many small groups scattered over a large area, so
    // legitimately simple single-chamber rooms (z2_zolashop_0 at 36 verts, z2_redead_0, z2_inisie_bs_0)
    // failed with spread ~1.0 despite being perfectly well-formed. Orphaned sepds are a real
    // structural invariant instead of a shape assumption, and are exactly what the MM3D mesh-stride
    // bug produced (27 of 41 sepds unreachable -> the room's ground silently never built) while every
    // finiteness/extent check still passed.
    printf("\norphan sepds — OoT3D: %zu, MM3D: %zu (both must be 0)\n", oot.orphanSepds, mm.orphanSepds);
    if (oot.orphanSepds != 0 || mm.orphanSepds != 0) {
        printf("RESULT: FAIL — sepds unreachable from any mesh entry; that geometry never builds.\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
