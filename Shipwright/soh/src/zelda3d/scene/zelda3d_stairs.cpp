// Zelda3D procedural stair geometry (see zelda3d_stairs.h). Moved verbatim out of zelda3d_model.cpp.
#include "zelda3d_stairs.h"
#include "asset/cmb.h"
#include <stb_image.h>
#include "../assets/stairs_stone_png.h" // embedded PNG of assets/zelda3d/stairs_stone.svg (custom stair texture)
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

float gZelda3dStairRiserY = 14.0f;
int gZelda3dStairs = 1; // env ZELDA3D_STAIRS / REPL `stairs` gate (default on)

// Decode the embedded custom stair texture (PNG -> RGBA8) once. Returns the cached pixel
// buffer + dims; w/h = 0 on failure (then the steps fall back to no texture / vertex color).
const std::vector<uint8_t>& stairStoneTex(int& w, int& h) {
    static std::vector<uint8_t> rgba;
    static int sw = 0, sh = 0, tried = 0;
    if (!tried) {
        tried = 1;
        int n = 0;
        stbi_uc* px = stbi_load_from_memory(kStairStonePng, (int)kStairStonePngLen, &sw, &sh, &n, 4);
        if (px) {
            rgba.assign(px, px + (size_t)sw * sh * 4);
            stbi_image_free(px);
        } else {
            fprintf(stderr, "[Zelda3D] stairs: failed to decode embedded stone texture\n");
            sw = sh = 0;
        }
    }
    w = sw; h = sh;
    return rgba;
}

bool texNameIsKaidan(const Zelda3D::Cmb& cmb, int matIndex) {
    if (matIndex < 0 || matIndex >= (int)cmb.materials().size()) return false;
    int ti = cmb.materials()[matIndex].tex0_idx;
    if (ti < 0 || ti >= (int)cmb.textures().size()) return false;
    return cmb.textures()[ti].name.find("kaidan") != std::string::npos;
}

// Solve a 3x3 linear system A x = b (Gaussian elimination, partial pivot). Returns false
// if singular. Used to fit the ramp's affine UV(a,c) map so generated step verts inherit
// the original texture coordinates.
static bool solve3(double A[3][3], double b[3], double x[3]) {
    for (int col = 0; col < 3; col++) {
        int piv = col;
        for (int r = col + 1; r < 3; r++)
            if (std::fabs(A[r][col]) > std::fabs(A[piv][col])) piv = r;
        if (std::fabs(A[piv][col]) < 1e-12) return false;
        if (piv != col) {
            for (int k = 0; k < 3; k++) std::swap(A[piv][k], A[col][k]);
            std::swap(b[piv], b[col]);
        }
        for (int r = col + 1; r < 3; r++) {
            double f = A[r][col] / A[col][col];
            for (int k = col; k < 3; k++) A[r][k] -= f * A[col][k];
            b[r] -= f * b[col];
        }
    }
    for (int r = 2; r >= 0; r--) {
        double s = b[r];
        for (int k = r + 1; k < 3; k++) s -= A[r][k] * x[k];
        x[r] = s / A[r][r];
    }
    return true;
}

// ---- shared kaidan-patch analysis (used by BOTH the render-side step geometry and the
// collision-side stepped floor in zelda3d.c, so the two never diverge). A kaidan group's flat
// triangles are split into connected, coplanar ramp patches; each sloped patch yields a step
// frame (ascend/across axes, footprint bbox, step count). ----

// Per-triangle outward normal (CCW winding -> outward, matches the CMB convention).
std::vector<std::array<float, 3>> stairTriNormals(const Zelda3D::CmbDrawGroup& g) {
    size_t ntri = g.verts.size() / 3;
    std::vector<std::array<float, 3>> nrm(ntri);
    for (size_t t = 0; t < ntri; t++) {
        const float* p0 = g.verts[3 * t + 0].pos;
        const float* p1 = g.verts[3 * t + 1].pos;
        const float* p2 = g.verts[3 * t + 2].pos;
        float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
        float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
        float* n = nrm[t].data();
        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l > 1e-9f) { n[0] /= l; n[1] /= l; n[2] /= l; }
    }
    return nrm;
}

// Union-find: connect triangles that SHARE a vertex AND are ~coplanar (normal dot > 0.98).
// Separate staircases (no shared verts) and touching ramps of different orientation stay
// distinct patches. Returns the patches as lists of triangle indices.
std::vector<std::vector<int>> stairPatches(const Zelda3D::CmbDrawGroup& g,
                                                  const std::vector<std::array<float, 3>>& nrm) {
    size_t ntri = g.verts.size() / 3;
    std::vector<int> parent(ntri);
    for (size_t i = 0; i < ntri; i++) parent[i] = (int)i;
    std::function<int(int)> find = [&](int a) {
        while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    };
    auto coplanar = [&](size_t a, size_t b) {
        return nrm[a][0] * nrm[b][0] + nrm[a][1] * nrm[b][1] + nrm[a][2] * nrm[b][2] > 0.98f;
    };
    std::unordered_map<uint64_t, int> vmap; // quantized vertex -> a triangle that touches it
    auto vkey = [](const float* p) -> uint64_t {
        auto q = [](float v) -> uint64_t { return (uint64_t)(int64_t)std::llround(v / 2.0f) & 0x1FFFFF; };
        return (q(p[0]) << 42) | (q(p[1]) << 21) | q(p[2]);
    };
    for (size_t t = 0; t < ntri; t++) {
        for (int k = 0; k < 3; k++) {
            uint64_t key = vkey(g.verts[3 * t + k].pos);
            auto it = vmap.find(key);
            if (it != vmap.end() && coplanar(t, (size_t)it->second)) {
                parent[find((int)t)] = find(it->second);
            }
            vmap[key] = (int)t; // last writer; union above stitches the chain
        }
    }
    std::unordered_map<int, std::vector<int>> byroot;
    for (size_t t = 0; t < ntri; t++) byroot[find((int)t)].push_back((int)t);
    std::vector<std::vector<int>> out;
    out.reserve(byroot.size());
    for (auto& kv : byroot) out.push_back(std::move(kv.second));
    return out;
}

// A single staircase patch's stepping frame, derived purely from geometry (identical between
// render + collision). aDir = horizontal uphill, cDir = across; the footprint is the (a,c) bbox
// and the patch rises ymin..ymax in N steps of (da,dy).

// Compute the step frame for one patch. Returns false (caller keeps the flat tris) when the
// patch is not a walkable slope (flat floor / near-vertical wall) or is degenerate.
bool stairFrameOf(const Zelda3D::CmbDrawGroup& g, const std::vector<int>& tris,
                         const std::vector<std::array<float, 3>>& nrm, StairFrame& f) {
    float n[3] = { 0, 0, 0 };
    for (int t : tris) { n[0] += nrm[t][0]; n[1] += nrm[t][1]; n[2] += nrm[t][2]; }
    float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (nl < 1e-6f) return false;
    n[0] /= nl; n[1] /= nl; n[2] /= nl;

    // Only stair-able FLOORS: upward-facing and actually sloped (a flat floor or a near-vertical
    // wall is not a ramp).
    if (!(n[1] > 0.5f && (n[0] * n[0] + n[2] * n[2]) > 0.02f)) return false;

    float ah = std::sqrt(n[0] * n[0] + n[2] * n[2]);
    f.aDir[0] = -n[0] / ah; f.aDir[1] = 0.0f; f.aDir[2] = -n[2] / ah; // uphill gradient
    f.cDir[0] = f.aDir[2]; f.cDir[1] = 0.0f; f.cDir[2] = -f.aDir[0];  // across, in XZ

    f.amin = f.cmin = f.ymin = 1e30f;
    f.amax = f.cmax = f.ymax = -1e30f;
    for (int t : tris) {
        for (int k = 0; k < 3; k++) {
            const float* p = g.verts[3 * t + k].pos;
            float a = f.aDir[0] * p[0] + f.aDir[2] * p[2];
            float c = f.cDir[0] * p[0] + f.cDir[2] * p[2];
            f.amin = std::min(f.amin, a); f.amax = std::max(f.amax, a);
            f.cmin = std::min(f.cmin, c); f.cmax = std::max(f.cmax, c);
            f.ymin = std::min(f.ymin, p[1]); f.ymax = std::max(f.ymax, p[1]);
        }
    }
    if (f.amax - f.amin < 1.0f || f.cmax - f.cmin < 1.0f || f.ymax - f.ymin < 1.0f) return false;

    f.N = (int)std::lround((f.ymax - f.ymin) / (gZelda3dStairRiserY > 0.5f ? gZelda3dStairRiserY : 0.5f));
    if (f.N < 1) f.N = 1;
    if (f.N > 200) f.N = 200;
    f.da = (f.amax - f.amin) / f.N;
    f.dy = (f.ymax - f.ymin) / f.N;
    return true;
}

// Replace one kaidan draw group's flat-ramp triangles with stepped geometry. Each
// connected, coplanar patch (a single ramp; one group may hold several separate
// staircases) is rebuilt as treads+risers over its footprint.
void generateStairsGroup(Zelda3D::CmbDrawGroup& g) {
    size_t ntri = g.verts.size() / 3;
    if (ntri < 2) return;

    std::vector<std::array<float, 3>> nrm = stairTriNormals(g);
    std::vector<std::vector<int>> patches = stairPatches(g, nrm);

    std::vector<Zelda3D::CmbVertex> outv;
    outv.reserve(g.verts.size() * 4);

    static int stairDbg = -1;
    if (stairDbg < 0) { const char* e = getenv("ZELDA3D_STAIRDBG"); stairDbg = (e && *e) ? atoi(e) : 0; }

    for (const std::vector<int>& tris : patches) {
        StairFrame f;
        bool ok = stairFrameOf(g, tris, nrm, f);
        if (stairDbg && ok) {
            float ac = (f.amin + f.amax) * 0.5f, cc = (f.cmin + f.cmax) * 0.5f;
            float wx = f.aDir[0] * ac + f.cDir[0] * cc, wz = f.aDir[2] * ac + f.cDir[2] * cc;
            fprintf(stderr, "[Zelda3D] stairdbg: patch world XZ=(%.0f,%.0f) y=[%.0f,%.0f] N=%d aSpan=%.0f cSpan=%.0f "
                   "aDir=(%.2f,%.2f) cDir=(%.2f,%.2f)\n",
                   wx, wz, f.ymin, f.ymax, f.N, f.amax - f.amin, f.cmax - f.cmin,
                   f.aDir[0], f.aDir[2], f.cDir[0], f.cDir[2]);
            fflush(stdout);
        }

        // Affine UV(a,c) fit + average color over the patch (render-only; the generated step
        // verts inherit the original ramp's texture coordinates and baked lighting).
        double M[3][3] = {}, bu[3] = {}, bv[3] = {};
        float col[4] = {}; int nc = 0;
        const Zelda3D::CmbVertex& src = g.verts[3 * tris[0]];
        if (ok) {
            for (int t : tris) {
                for (int k = 0; k < 3; k++) {
                    const Zelda3D::CmbVertex& v = g.verts[3 * t + k];
                    float a = f.aDir[0] * v.pos[0] + f.aDir[2] * v.pos[2];
                    float c = f.cDir[0] * v.pos[0] + f.cDir[2] * v.pos[2];
                    double r[3] = { a, c, 1.0 };
                    for (int i = 0; i < 3; i++) {
                        for (int j = 0; j < 3; j++) M[i][j] += r[i] * r[j];
                        bu[i] += r[i] * v.uv[0];
                        bv[i] += r[i] * v.uv[1];
                    }
                    for (int e = 0; e < 4; e++) col[e] += v.color[e];
                    nc++;
                }
            }
        }
        double mu[3], mv[3];
        if (ok) {
            for (int e = 0; e < 4; e++) col[e] /= (float)nc;
            double Mu[3][3], Mv[3][3];
            std::memcpy(Mu, M, sizeof(M)); std::memcpy(Mv, M, sizeof(M));
            ok = solve3(Mu, bu, mu) && solve3(Mv, bv, mv);
        }
        if (!ok) { // not a slope (or degenerate UV) -> keep the original tris verbatim
            for (int t : tris)
                for (int k = 0; k < 3; k++) outv.push_back(g.verts[3 * t + k]);
            continue;
        }

        // Emit a vertex: geometry at (aGeom, y, c) with explicit UV into the CUSTOM stair
        // texture (stairs_stone.svg). The texture is a single STEP tile: V in [0,Vnose) is
        // the tread (top surface), V == Vnose is the lit nosing line, V in (Vnose,1] is the
        // riser (front face). One geometry step maps to one tile in V; U tiles horizontally
        // across the staircase width (REPEAT) at kStairTileW world-units per tile. Vertex
        // color is forced white so the authored stone shows true (the kaidan baked color is
        // irrelevant now that we no longer use its texture).
        // Step color = the original kaidan ramp's averaged baked vertex color (col[], averaged over
        // the patch above) so the steps sit in the SAME scene lighting/tone as the ramp they replace
        // — NOT flat white (which read pale/unlit). emit multiplies in a per-face shade (stepShade)
        // for 3D readout: treads catch the most light, risers less, the side caps least.
        float baseCol[4] = { col[0], col[1], col[2], col[3] };
        float stepShade = 1.0f; // set per face below
        auto emit = [&](float aGeom, float y, float c, float u, float vtex, const float nrmv[3]) {
            Zelda3D::CmbVertex v = src;
            v.pos[0] = f.aDir[0] * aGeom + f.cDir[0] * c;
            v.pos[1] = y;
            v.pos[2] = f.aDir[2] * aGeom + f.cDir[2] * c;
            v.nrm[0] = nrmv[0]; v.nrm[1] = nrmv[1]; v.nrm[2] = nrmv[2];
            v.uv[0] = u;
            v.uv[1] = vtex;
            v.color[0] = baseCol[0] * stepShade;
            v.color[1] = baseCol[1] * stepShade;
            v.color[2] = baseCol[2] * stepShade;
            v.color[3] = baseCol[3];
            outv.push_back(v);
        };
        (void)mu; (void)mv; // affine fit kept only as the degeneracy gate above
        const float SH_TREAD = 1.00f, SH_RISER = 0.72f, SH_SIDE = 0.55f; // per-face shade
        const float nUp[3] = { 0, 1, 0 };
        const float nDn[3] = { -f.aDir[0], 0, -f.aDir[2] }; // riser faces downhill (toward the climber)
        const float nCmin[3] = { -f.cDir[0], 0, -f.cDir[2] }; // side wall at cmin faces -c
        const float nCmax[3] = {  f.cDir[0], 0,  f.cDir[2] }; // side wall at cmax faces +c
        const float kTileW = 44.0f;   // world units per horizontal texture tile
        const float Vnose = 0.62f;    // tread/riser split in the texture (matches the SVG)
        const float uMin = f.cmin / kTileW, uMax = f.cmax / kTileW;
        // #1: raise the whole flight by a FULL step (user, 2026-06-20: "move elevation from d/2 to d")
        // so the treads sit a step above the original ramp diagonal and the top tread reaches the
        // upper ground. yr=dy => the tread top yk = ymin+(k+1)*dy lands exactly on the ORIGINAL ramp
        // diagonal at a1 (yRamp(a1) = ymin + (a1-amin)*dy/da = ymin+(k+1)*dy). So the stepped upper
        // surface and the ramp diagonal coincide at every step's back edge and diverge by at most one
        // step in front of it — the solid occupies ONLY the thin wedge between the steps and the ramp
        // they replace, exactly the envelope of the old flat ramp. The side caps are per-step wedge
        // TRIANGLES bounded above by the tread and below by the ramp diagonal — NOT rectangles down to
        // ymin (those buried the flanking brick wall, #1 follow-up). The closed stepped-vs-ramp wedge
        // still admits no sky bleed (#1 cyan halo) from normal viewing angles, while leaving the wall
        // behind fully visible.
        const float yr = f.dy;
        for (int k = 0; k < f.N; k++) {
            float a0 = f.amin + k * f.da, a1 = f.amin + (k + 1) * f.da;
            // yk/yk1 = the treads/risers, raised a full step above the original ramp diagonal. The
            // riser climbs at the BACK of the tread (a1) up to the next tread's height.
            float yk = f.ymin + k * f.dy + yr, yk1 = f.ymin + (k + 1) * f.dy + yr;
            // Tread (top face, +Y) at yk: front edge a0 = nosing (V=Vnose) -> back a1 = V=0.
            stepShade = SH_TREAD;
            emit(a0, yk, f.cmin, uMin, Vnose, nUp); emit(a1, yk, f.cmin, uMin, 0.0f, nUp); emit(a1, yk, f.cmax, uMax, 0.0f, nUp);
            emit(a0, yk, f.cmin, uMin, Vnose, nUp); emit(a1, yk, f.cmax, uMax, 0.0f, nUp); emit(a0, yk, f.cmax, uMax, Vnose, nUp);
            // Riser (front face, -aDir) at a1, yk -> yk1: top yk1 = nosing (V=Vnose), bottom yk = V=1.
            // (Skip the last step's riser; the top tread meets the upper ground at amax, no face past it.)
            if (k + 1 < f.N) {
                stepShade = SH_RISER;
                emit(a1, yk, f.cmin, uMin, 1.0f, nDn); emit(a1, yk1, f.cmin, uMin, Vnose, nDn); emit(a1, yk1, f.cmax, uMax, Vnose, nDn);
                emit(a1, yk, f.cmin, uMin, 1.0f, nDn); emit(a1, yk1, f.cmax, uMax, Vnose, nDn); emit(a1, yk, f.cmax, uMax, 1.0f, nDn);
            }
            // Side cap: the thin WEDGE between this step and the original ramp diagonal, on each c
            // edge — a single TRIANGLE per step (NOT a rectangle down to ymin, which buried the
            // flanking brick wall, #1 follow-up). The ramp diagonal is yRamp(a)=ymin+(a-amin)*dy/da,
            // so yRamp(a0)=ymin+k*dy and yRamp(a1)=ymin+(k+1)*dy=yk (since yr=dy). The wedge corners:
            //   P1 (a0, yRamp(a0)=ymin+k*dy) — front-bottom, on the ramp / base of this riser front
            //   P2 (a0, yk)                  — front-top, top of the riser / front edge of the tread
            //   P3 (a1, yk)                  — back, where the tread rejoins the ramp (yk=yRamp(a1))
            // Consecutive steps share P1 with the previous step's P3 along the ramp, so the triangles
            // tile the steps-vs-ramp region with no gap and no overhang below the ramp surface — the
            // staircase occupies exactly the old flat ramp's envelope, leaving the brick wall behind
            // fully visible. The ramp diagonal closes the underside, so no separate bottom/back plane
            // is needed and no sky bleeds through from normal viewing angles (#1 cyan halo).
            stepShade = SH_SIDE;
            float uA0 = a0 / kTileW, uA1 = a1 / kTileW;
            float yRamp0 = f.ymin + (float)k * f.dy; // ramp surface at a0 (one step below the tread)
            // cmin side faces -c (outward). CCW seen from -c: P1 -> P3 -> P2.
            emit(a0, yRamp0, f.cmin, uA0, 1.0f, nCmin); emit(a1, yk, f.cmin, uA1, Vnose, nCmin); emit(a0, yk, f.cmin, uA0, Vnose, nCmin);
            // cmax side faces +c (outward, opposite winding): P1 -> P2 -> P3.
            emit(a0, yRamp0, f.cmax, uA0, 1.0f, nCmax); emit(a0, yk, f.cmax, uA0, Vnose, nCmax); emit(a1, yk, f.cmax, uA1, Vnose, nCmax);

            // BOTTOM-FRONT SEAL (first step only). Each step's riser is emitted at the BACK of its
            // tread (a1), so the very FIRST step has no downhill face at its front (a=amin): the
            // volume between the first tread (ymin+dy) and the ramp base (ymin) is left OPEN there,
            // and the OoT3D sky bleeds through that gap at the bottom of the flight (user: "first
            // step missing its Y vertex" — a cyan wedge under the bottom step). Close it with one
            // downhill-facing front wall at a=amin spanning the full width, from the first tread top
            // (yk) down to the ramp base (ymin) — one step tall, flush with the ground, so nothing
            // below ymin is added and the flanking wall stays visible.
            if (k == 0) {
                stepShade = SH_RISER;
                emit(a0, f.ymin, f.cmin, uMin, 1.0f, nDn); emit(a0, yk, f.cmin, uMin, Vnose, nDn); emit(a0, yk, f.cmax, uMax, Vnose, nDn);
                emit(a0, f.ymin, f.cmin, uMin, 1.0f, nDn); emit(a0, yk, f.cmax, uMax, Vnose, nDn); emit(a0, f.ymin, f.cmax, uMax, 1.0f, nDn);
            }
        }
    }
    g.verts.swap(outv);
}

// ZELDA3D_STAIRS env -> gZelda3dStairs, parsed once. Called from both the render path
// (generateRoomStairs) and the collision collector, since either can run first.
void ensureStairsEnv() {
    static int envChecked = 0;
    if (!envChecked) {
        envChecked = 1;
        const char* e = getenv("ZELDA3D_STAIRS");
        if (e && *e) gZelda3dStairs = atoi(e);
    }
}
