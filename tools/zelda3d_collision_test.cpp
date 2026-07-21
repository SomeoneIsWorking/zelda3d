// Scene-collision well-formedness check for OoT3D and MM3D scene ZSIs.
//
// WHY: MM now RENDERS 3DS room geometry but still COLLIDES against the N64 mesh, so anywhere the
// 3DS remodel moved a surface the player floats or clips (measured: in South Clock Town Link rests
// at y=25.4 where the 3DS room mesh has a single surface at y=0.0). Porting MM3D collision is the
// fix, and the first question is whether MM3D's collision layout is the SAME as OoT3D's — the
// shared parser (asset/zcol.h) was reverse-engineered against OoT3D only, and MM3D has already
// burned us twice with version-gated CMB fields (index-slot scaling, mesh stride).
//
// This does NOT assume it. The layout is self-validating: for a correctly parsed CollisionPoly the
// stored plane must satisfy n . vA == -dist, and the stored normal must equal the geometric face
// normal of (vA, vB, vC). Those hold ~100% / 99.9% on OoT3D (see zcol.h). If MM3D parses with the
// same invariants holding, the layout matches; if it parses into garbage, they collapse and this
// test says so instead of shipping a wrong collision mesh.
//
// OoT3D is the KNOWN-GOOD reference in the same run, so a regression in the shared parser is
// distinguishable from an MM3D-specific layout difference.
//
// Build: tools/build_asset_test.sh
// Run:   ZELDA3D_OOT3D_ROM=... ZELDA3D_MM3D_ROM=... scratch/bin/collision_test [ootScene] [mmScene]
#include "asset/ctr_rom.h"
#include "asset/lzs.h"
#include "asset/zcol.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace Zelda3D;

namespace {

struct ColStats {
    bool ok = false;
    std::string err;
    size_t verts = 0, polys = 0, surfs = 0;
    double planeOk = 0.0;   // fraction satisfying n . vA == -dist
    double normalOk = 0.0;  // fraction whose stored normal matches the geometric face normal
    size_t badIndex = 0;    // polys referencing a vertex outside the vertex array
    size_t degenerate = 0;  // zero-area triangles (can't check a normal against them)
    float lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
};

void reportExits(const char* label, const OoT3DCollision& col);
void nodeEstimate(const char* label, const OoT3DCollision& col);

ColStats analyze(const char* romEnv, const std::string& path) {
    ColStats s;
    const char* rom = getenv(romEnv);
    if (!rom || !*rom) { s.err = std::string("set ") + romEnv; return s; }
    CtrRom r(rom);
    if (!r.ok()) { s.err = "CtrRom: " + r.error(); return s; }
    auto bytes = r.read(path);
    if (bytes.empty()) { s.err = "not found: " + path; return s; }
    // MM3D scene ZSIs are LzS-compressed; OoT3D's are raw.
    if (LzsIsCompressed(bytes)) {
        std::string e;
        auto inflated = LzsDecompress(bytes, &e);
        if (inflated.empty()) { s.err = "LzS: " + e; return s; }
        bytes = std::move(inflated);
    }
    OoT3DCollision col(bytes);
    if (!col.ok()) { s.err = col.error(); return s; }
    if (getenv("ZELDA3D_EXIT_CENSUS")) reportExits(path.c_str(), col);
    if (getenv("ZELDA3D_NODE_EST")) nodeEstimate(path.c_str(), col);

    const auto& V = col.verts();
    const auto& P = col.polys();
    s.verts = V.size();
    s.polys = P.size();
    s.surfs = col.surfaces().size();
    if (V.empty() || P.empty()) { s.err = "empty collision"; return s; }

    for (size_t i = 0; i < V.size(); i++) {
        const float p[3] = { (float)V[i].x, (float)V[i].y, (float)V[i].z };
        for (int k = 0; k < 3; k++) {
            if (i == 0 || p[k] < s.lo[k]) s.lo[k] = p[k];
            if (i == 0 || p[k] > s.hi[k]) s.hi[k] = p[k];
        }
    }

    size_t planeHits = 0, normalHits = 0, checked = 0;
    for (const auto& q : P) {
        if (q.vA >= V.size() || q.vB >= V.size() || q.vC >= V.size()) { s.badIndex++; continue; }
        const OoT3DCollision::Vert& a = V[q.vA];
        const OoT3DCollision::Vert& b = V[q.vB];
        const OoT3DCollision::Vert& c = V[q.vC];
        // stored normal, unit-scaled the way the format encodes it
        const double n[3] = { q.nx / 32767.0, q.ny / 32767.0, q.nz / 32767.0 };
        const double nlen = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (nlen < 1e-6) { s.degenerate++; continue; }

        // plane identity: n . vA == -dist  (tolerance is generous — vertices are integers and dist
        // is a float, so a correct parse still lands a unit or two off on large coordinates)
        const double dot = n[0] * a.x + n[1] * a.y + n[2] * a.z;
        if (std::fabs(dot + q.dist) <= 2.0) planeHits++;

        // geometric face normal of the triangle
        const double u[3] = { (double)(b.x - a.x), (double)(b.y - a.y), (double)(b.z - a.z) };
        const double v[3] = { (double)(c.x - a.x), (double)(c.y - a.y), (double)(c.z - a.z) };
        const double g[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                              u[0] * v[1] - u[1] * v[0] };
        const double glen = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
        if (glen < 1e-6) { s.degenerate++; continue; }
        const double cosang = (g[0] * n[0] + g[1] * n[1] + g[2] * n[2]) / (glen * nlen);
        if (cosang > 0.98) normalHits++; // within ~11 degrees, same facing
        checked++;
    }
    if (checked > 0) {
        s.planeOk = (double)planeHits / (double)checked;
        s.normalOk = (double)normalHits / (double)checked;
    }
    s.ok = true;
    return s;
}

// Exit-poly census. MM keeps the scene exit index in SurfaceType data0 bits 8..12
// (SURFACETYPE0's exitIndex field, z64bgcheck.h). Listing which polys carry a non-zero exit — and
// where they are — is what makes an exit testable in-game without guessing at map layout, and it
// answers whether MM3D's surface types even encode exits the same way. ZELDA3D_EXIT_CENSUS=1.
void reportExits(const char* label, const OoT3DCollision& col) {
    const auto& V = col.verts();
    const auto& P = col.polys();
    const auto& S = col.surfaces();
    struct Acc { int n = 0; double x = 0, y = 0, z = 0; };
    std::vector<Acc> byExit(32);
    for (const auto& q : P) {
        if (q.type >= S.size()) continue;
        const unsigned exitIdx = (S[q.type].data0 >> 8) & 0x1F;
        if (exitIdx == 0) continue;
        if (q.vA >= V.size() || q.vB >= V.size() || q.vC >= V.size()) continue;
        Acc& a = byExit[exitIdx];
        a.n++;
        a.x += (V[q.vA].x + V[q.vB].x + V[q.vC].x) / 3.0;
        a.y += (V[q.vA].y + V[q.vB].y + V[q.vC].y) / 3.0;
        a.z += (V[q.vA].z + V[q.vB].z + V[q.vC].z) / 3.0;
    }
    int total = 0;
    for (size_t i = 0; i < byExit.size(); i++) {
        if (byExit[i].n == 0) continue;
        total++;
        printf("%-34s   exit[%2zu]: %3d polys, centroid (%.0f, %.0f, %.0f)\n", label, i, byExit[i].n,
               byExit[i].x / byExit[i].n, byExit[i].y / byExit[i].n, byExit[i].z / byExit[i].n);
    }
    if (total == 0) printf("%-34s   NO exit-bearing polys found\n", label);
}

// Static-lookup node-requirement estimate. z_bgcheck inserts ONE SSNode per (poly, intersecting
// subdivision cell); its pool is sized from a per-scene memSize budget tuned to the N64 mesh. The
// denser MM3D mesh can need far more, and overflowing the pool hangs scene load inside
// StaticLookup_AddPolyToSSList. This counts the (poly, cell) pairs by AABB overlap — the same
// quantity, minus z_bgcheck's exact triangle test, so it is a close upper bound.
//   ZELDA3D_NODE_EST="sx,sy,sz"  (subdivAmount, e.g. Termina Field's 36,1,36; default 16,4,16)
void nodeEstimate(const char* label, const OoT3DCollision& col) {
    int sa[3] = { 16, 4, 16 };
    if (const char* q = getenv("ZELDA3D_NODE_EST")) sscanf(q, "%d,%d,%d", &sa[0], &sa[1], &sa[2]);
    const auto& V = col.verts();
    const auto& P = col.polys();
    if (V.empty() || P.empty()) return;
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (const auto& v : V) {
        const float c[3] = { (float)v.x, (float)v.y, (float)v.z };
        for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], c[k]); hi[k] = std::max(hi[k], c[k]); }
    }
    // BgCheck_SetSubdivisionDimension: subdivLength = (int)(length / subdivAmount) + 1, min-clamped.
    float len[3];
    for (int k = 0; k < 3; k++) {
        len[k] = (float)((int)((hi[k] - lo[k]) / sa[k]) + 1);
        if (len[k] < 1.0f) len[k] = 1.0f;
    }
    long pairs = 0;
    for (const auto& q : P) {
        if (q.vA >= V.size() || q.vB >= V.size() || q.vC >= V.size()) continue;
        const OoT3DCollision::Vert* t[3] = { &V[q.vA], &V[q.vB], &V[q.vC] };
        long span = 1;
        for (int k = 0; k < 3; k++) {
            float pmin = 1e30f, pmax = -1e30f;
            for (int j = 0; j < 3; j++) {
                const float c = (k == 0) ? t[j]->x : (k == 1) ? t[j]->y : t[j]->z;
                pmin = std::min(pmin, c);
                pmax = std::max(pmax, c);
            }
            // 50-unit overlap on each side, matching BGCHECK_SUBDIV_OVERLAP
            int a = (int)((pmin - 50.0f - lo[k]) / len[k]);
            int b = (int)((pmax + 50.0f - lo[k]) / len[k]);
            a = std::max(a, 0);
            b = std::min(b, sa[k] - 1);
            span *= (b >= a) ? (b - a + 1) : 1;
        }
        pairs += span;
    }
    printf("%-34s   node estimate: %ld (poly,cell) pairs for %zu polys over %dx%dx%d cells (%.1f/poly)\n",
           label, pairs, P.size(), sa[0], sa[1], sa[2], (double)pairs / (double)P.size());
}

void report(const char* label, const ColStats& s) {
    if (!s.ok) { printf("%-34s FAILED: %s\n", label, s.err.c_str()); return; }
    printf("%-34s verts=%6zu polys=%6zu surfaces=%3zu  plane=%.1f%% normal=%.1f%% badIdx=%zu degen=%zu\n",
           label, s.verts, s.polys, s.surfs, s.planeOk * 100.0, s.normalOk * 100.0, s.badIndex,
           s.degenerate);
    printf("%-34s   bbox x[%.0f %.0f] y[%.0f %.0f] z[%.0f %.0f]\n", "", s.lo[0], s.hi[0], s.lo[1],
           s.hi[1], s.lo[2], s.hi[2]);
}

} // namespace

int main(int argc, char** argv) {
    const std::string ootScene = (argc > 1) ? argv[1] : "ydan";
    const std::string mmScene = (argc > 2) ? argv[2] : "z2_clocktower";
    const std::string ootPath = "/scene/" + ootScene + "_info.zsi";
    const std::string mmPath = "/scenes/" + mmScene + "_info.zsi";

    ColStats oot = analyze("ZELDA3D_OOT3D_ROM", ootPath);
    ColStats mm = analyze("ZELDA3D_MM3D_ROM", mmPath);
    printf("REFERENCE (OoT3D, collision already ported and shipping):\n  ");
    report(ootPath.c_str(), oot);
    printf("UNDER TEST (MM3D):\n  ");
    report(mmPath.c_str(), mm);

    if (!oot.ok) {
        printf("\nRESULT: INCONCLUSIVE — the OoT3D reference scene failed to parse (%s)\n",
               oot.err.c_str());
        return 2;
    }
    // Guard the reference first: if the shared parser regressed, that is the finding.
    if (oot.planeOk < 0.95 || oot.normalOk < 0.95) {
        printf("\nRESULT: FAIL — the OoT3D REFERENCE collision no longer validates "
               "(plane %.1f%%, normal %.1f%%); the shared parser regressed.\n",
               oot.planeOk * 100.0, oot.normalOk * 100.0);
        return 1;
    }
    if (!mm.ok) {
        printf("\nRESULT: FAIL — MM3D scene collision did not parse: %s\n", mm.err.c_str());
        return 1;
    }
    // The real question: does MM3D use the same layout?
    if (mm.badIndex != 0) {
        printf("\nRESULT: FAIL — %zu MM3D collision polys reference out-of-range vertices; the "
               "layout differs from OoT3D's.\n", mm.badIndex);
        return 1;
    }
    if (mm.planeOk < 0.95 || mm.normalOk < 0.95) {
        printf("\nRESULT: FAIL — MM3D collision does not satisfy the format's own invariants "
               "(plane %.1f%%, normal %.1f%%; OoT3D reference: %.1f%% / %.1f%%).\n"
               "        The bytes parse but do not describe consistent planes, so MM3D's collision\n"
               "        layout is NOT OoT3D's. Do not install this mesh — re-derive the layout.\n",
               mm.planeOk * 100.0, mm.normalOk * 100.0, oot.planeOk * 100.0, oot.normalOk * 100.0);
        return 1;
    }
    printf("\nRESULT: PASS — MM3D collision parses with OoT3D's layout and satisfies the plane and\n"
           "        face-normal invariants, so the shared parser can drive MM collision.\n");
    return 0;
}
