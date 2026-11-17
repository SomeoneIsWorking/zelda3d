// Zelda3D procedural stair geometry (see zelda3d_stairs.h). Moved verbatim out of zelda3d_model.cpp.
#include "zelda3d_stairs.h"
#include "asset/cmb.h"
#include "asset/pica_texture.h"
#include <stb_image.h>
#include "../assets/stairs_stone_png.h" // embedded PNG of assets/zelda3d/stairs_stone.svg (custom stair texture)
#include "../core/zelda3d_log.h"
#include <algorithm>
#include <complex>
#include <map>
#include <string>
#include <functional>
#include <unordered_map>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

float gZelda3dStairRiserY = 8.0f;
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
            Z3D_LOG(SOH_SCENE, "stairs: failed to decode embedded stone texture\n");
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

namespace {
// A curb column's step-period strength is below this share of the strong columns', and a curb is at
// most this share of the texture wide.
constexpr float kCurbStrength = 0.4f;
constexpr float kMaxCurb = 0.25f;

// |sum (x - mean) e^(-2 pi i k n / count)|: the strength of period count/k in a row or column.
float periodStrength(const std::vector<float>& values, int k) {
    float mean = 0.0f;
    for (float v : values)
        mean += v;
    mean /= (float)values.size();
    std::complex<double> sum = 0.0;
    const double step = -2.0 * M_PI * k / (double)values.size();
    for (size_t n = 0; n < values.size(); n++)
        sum += (double)(values[n] - mean) * std::polar(1.0, step * (double)n);
    return (float)std::abs(sum);
}

StairCurbs detectCurbs(const std::vector<uint8_t>& rgba, int w, int h) {
    StairCurbs curbs;
    if (w < 4 || h < 8 || rgba.size() < (size_t)w * h * 4)
        return curbs;
    auto lum = [&](int x, int y) {
        const uint8_t* p = &rgba[((size_t)y * w + x) * 4];
        return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
    };
    // Steps per tile: the strongest period down the rows.
    std::vector<float> rows(h, 0.0f);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
            rows[y] += lum(x, y);
        rows[y] /= (float)w;
    }
    int steps = 2;
    float best = -1.0f;
    for (int k = 2; k < h / 4; k++) {
        const float s = periodStrength(rows, k);
        if (s > best) {
            best = s;
            steps = k;
        }
    }
    // Edge columns where that period is weak are the curbs.
    std::vector<float> strength(w);
    std::vector<float> column(h);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++)
            column[y] = lum(x, y);
        strength[x] = periodStrength(column, steps);
    }
    std::vector<float> sorted = strength;
    std::sort(sorted.begin(), sorted.end());
    const float strong = sorted[(size_t)w * 3 / 4] * kCurbStrength;
    int first = 0;
    while (first < w && strength[first] < strong)
        first++;
    int last = w;
    while (last > first && strength[last - 1] < strong)
        last--;
    if (first > w * kMaxCurb)
        first = 0;
    if (w - last > w * kMaxCurb)
        last = w;
    curbs.first = (float)first / (float)w;
    curbs.last = (float)last / (float)w;
    return curbs;
}
} // namespace

StairCurbs stairTextureCurbs(const Zelda3D::Cmb& cmb, int matIndex) {
    static std::map<std::string, StairCurbs> cache;
    if (matIndex < 0 || matIndex >= (int)cmb.materials().size())
        return {};
    const int ti = cmb.materials()[matIndex].tex0_idx;
    if (ti < 0 || ti >= (int)cmb.textures().size())
        return {};
    const Zelda3D::CmbTexture& tex = cmb.textures()[ti];
    auto found = cache.find(tex.name);
    if (found != cache.end())
        return found->second;
    const StairCurbs curbs = detectCurbs(
        Zelda3D::PicaDecode(tex.glFormat(), tex.width, tex.height, cmb.textureRaw(tex)), tex.width, tex.height);
    Z3D_LOG(SOH_SCENE, "stairs: %s curbs u<%.3f and u>%.3f\n", tex.name.c_str(), curbs.first, curbs.last);
    cache.emplace(tex.name, curbs);
    return curbs;
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
                  const std::vector<std::array<float, 3>>& nrm, const StairCurbs& curbs, StairFrame& f) {
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

    // The steps lie where the ramp's texture paints them and the painted curbs stay walls. A flight
    // is often several strips with their own u, so u is read from the triangle under each sample.
    const float amid = 0.5f * (f.amin + f.amax);
    auto paintedCurb = [&](float c) {
        float uv[2];
        stairRampUv(g, tris, f, amid, c, uv);
        const float fu = uv[0] - std::floor(uv[0]);
        return fu < curbs.first || fu > curbs.last;
    };
    constexpr int kSamples = 1024;
    constexpr float kMinSpan = 1.0f;
    const float w = (f.cmax - f.cmin) / kSamples;
    f.spans.clear();
    for (int i = 0; i < kSamples; i++) {
        const bool curb = (curbs.first > 0.0f || curbs.last < 1.0f) && paintedCurb(f.cmin + (i + 0.5f) * w);
        const float c1 = i + 1 == kSamples ? f.cmax : f.cmin + (i + 1) * w;
        if (!f.spans.empty() && f.spans.back().curb == curb) {
            f.spans.back().c1 = c1;
        } else {
            f.spans.push_back({ f.cmin + i * w, c1, curb });
        }
    }
    // A sliver too thin to read joins its neighbours.
    for (size_t i = 0; i < f.spans.size() && f.spans.size() > 1;) {
        if (f.spans[i].c1 - f.spans[i].c0 >= kMinSpan) {
            i++;
            continue;
        }
        if (i > 0) {
            f.spans[i - 1].c1 = f.spans[i].c1;
        } else {
            f.spans[1].c0 = f.spans[0].c0;
        }
        f.spans.erase(f.spans.begin() + (long)i);
        for (size_t j = 1; j < f.spans.size();) {
            if (f.spans[j].curb == f.spans[j - 1].curb) {
                f.spans[j - 1].c1 = f.spans[j].c1;
                f.spans.erase(f.spans.begin() + (long)j);
            } else {
                j++;
            }
        }
        i = 0;
    }
    return true;
}

void stairRampUv(const Zelda3D::CmbDrawGroup& g, const std::vector<int>& tris, const StairFrame& f, float a, float c,
                 float uv[2]) {
    // The triangle under (a, c), or past the patch's edge the one it lies least outside of.
    float best = -1e30f;
    for (int t : tris) {
        float A[3], C[3];
        for (int k = 0; k < 3; k++) {
            const Zelda3D::CmbVertex& v = g.verts[3 * t + k];
            A[k] = f.aDir[0] * v.pos[0] + f.aDir[2] * v.pos[2];
            C[k] = f.cDir[0] * v.pos[0] + f.cDir[2] * v.pos[2];
        }
        const float det = (C[1] - C[2]) * (A[0] - A[2]) + (A[2] - A[1]) * (C[0] - C[2]);
        if (std::fabs(det) < 1e-6f)
            continue;
        const float w0 = ((C[1] - C[2]) * (a - A[2]) + (A[2] - A[1]) * (c - C[2])) / det;
        const float w1 = ((C[2] - C[0]) * (a - A[2]) + (A[0] - A[2]) * (c - C[2])) / det;
        const float w2 = 1.0f - w0 - w1;
        const float inside = std::min(w0, std::min(w1, w2));
        if (inside <= best)
            continue;
        best = inside;
        const Zelda3D::CmbVertex* v = &g.verts[3 * t];
        uv[0] = w0 * v[0].uv[0] + w1 * v[1].uv[0] + w2 * v[2].uv[0];
        uv[1] = w0 * v[0].uv[1] + w1 * v[1].uv[1] + w2 * v[2].uv[1];
    }
}

// Replace one kaidan draw group's flat-ramp triangles with stepped geometry. Each
// connected, coplanar patch (a single ramp; one group may hold several separate
// staircases) is rebuilt as treads+risers over its footprint.
void generateStairsGroup(Zelda3D::CmbDrawGroup& g, const StairCurbs& curbs,
                         std::vector<Zelda3D::CmbVertex>& curbVerts) {
    size_t ntri = g.verts.size() / 3;
    if (ntri < 2) return;

    std::vector<std::array<float, 3>> nrm = stairTriNormals(g);
    std::vector<std::vector<int>> patches = stairPatches(g, nrm);

    std::vector<Zelda3D::CmbVertex> outv;
    outv.reserve(g.verts.size() * 4);
    curbVerts.clear();

    static int stairDbg = -1;
    if (stairDbg < 0) { const char* e = getenv("ZELDA3D_STAIRDBG"); stairDbg = (e && *e) ? atoi(e) : 0; }

    for (const std::vector<int>& tris : patches) {
        StairFrame f;
        bool ok = stairFrameOf(g, tris, nrm, curbs, f);
        if (stairDbg && ok) {
            float ac = (f.amin + f.amax) * 0.5f, cc = (f.cmin + f.cmax) * 0.5f;
            std::string curbList;
            for (const StairSpan& s : f.spans) {
                if (s.curb)
                    curbList += "[" + std::to_string((int)s.c0) + "," + std::to_string((int)s.c1) + "]";
            }
            float wx = f.aDir[0] * ac + f.cDir[0] * cc, wz = f.aDir[2] * ac + f.cDir[2] * cc;
            Z3D_LOG(
                SOH_SCENE,
                "stairdbg: patch world XZ=(%.0f,%.0f) y=[%.0f,%.0f] N=%d aSpan=%.0f c=[%.1f,%.1f] spans=%zu curbs=%s "
                "aDir=(%.2f,%.2f) cDir=(%.2f,%.2f)\n",
                wx, wz, f.ymin, f.ymax, f.N, f.amax - f.amin, f.cmin, f.cmax, f.spans.size(), curbList.c_str(),
                f.aDir[0], f.aDir[2], f.cDir[0], f.cDir[2]);
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
        // Steps go to the stone group; curbs keep the ramp's material and its own UVs.
        bool curbFace = false;
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
            (curbFace ? curbVerts : outv).push_back(v);
        };
        (void)mu; (void)mv; // affine fit kept only as the degeneracy gate above
        const float SH_TREAD = 1.00f, SH_RISER = 0.72f, SH_SIDE = 0.55f; // per-face shade
        const float nUp[3] = { 0, 1, 0 };
        const float nDn[3] = { -f.aDir[0], 0, -f.aDir[2] }; // riser faces downhill (toward the climber)
        const float nCmin[3] = { -f.cDir[0], 0, -f.cDir[2] }; // side wall at cmin faces -c
        const float nCmax[3] = {  f.cDir[0], 0,  f.cDir[2] }; // side wall at cmax faces +c
        const float kTileW = 44.0f;   // world units per horizontal texture tile
        const float Vnose = 0.62f;    // tread/riser split in the texture (matches the SVG)
        // Treads sit a full step above the ramp, so each meets the ramp diagonal at its back edge and
        // the stepped solid fills only the wedge between the steps and the ramp they replace.
        struct Corner {
            float a, y, c, u, v;
        };
        // One triangle facing `n`, wound counter-clockwise seen from that side.
        auto tri = [&](Corner p, Corner q, Corner r, const float n[3]) {
            auto world = [&](const Corner& k, float out[3]) {
                out[0] = f.aDir[0] * k.a + f.cDir[0] * k.c;
                out[1] = k.y;
                out[2] = f.aDir[2] * k.a + f.cDir[2] * k.c;
            };
            float P[3], Q[3], R[3];
            world(p, P);
            world(q, Q);
            world(r, R);
            const float e1[3] = { Q[0] - P[0], Q[1] - P[1], Q[2] - P[2] };
            const float e2[3] = { R[0] - P[0], R[1] - P[1], R[2] - P[2] };
            const float cx = e1[1] * e2[2] - e1[2] * e2[1], cy = e1[2] * e2[0] - e1[0] * e2[2],
                        cz = e1[0] * e2[1] - e1[1] * e2[0];
            if (cx * n[0] + cy * n[1] + cz * n[2] < 0.0f)
                std::swap(q, r);
            if (curbFace) {
                // The curb's paint, its faces unrolled onto the ramp: a run below the top maps back along it.
                for (Corner* k : { &p, &q, &r }) {
                    float uv[2];
                    stairRampUv(g, tris, f, k->a - (stairCurbTop(f, k->a) - k->y) * f.da / f.dy, k->c, uv);
                    k->u = uv[0];
                    k->v = uv[1];
                }
            }
            emit(p.a, p.y, p.c, p.u, p.v, n);
            emit(q.a, q.y, q.c, q.u, q.v, n);
            emit(r.a, r.y, r.c, r.u, r.v, n);
        };
        auto quad = [&](Corner p, Corner q, Corner r, Corner s, const float n[3]) {
            tri(p, q, r, n);
            tri(p, r, s, n);
        };
        for (size_t s = 0; s < f.spans.size(); s++) {
            const StairSpan& span = f.spans[s];
            const bool edgeLo = s == 0, edgeHi = s + 1 == f.spans.size();
            const float c0 = span.c0, c1 = span.c1, u0 = c0 / kTileW, u1 = c1 / kTileW;
            for (int k = 0; k < f.N; k++) {
                const float a0 = f.amin + k * f.da, a1 = f.amin + (k + 1) * f.da;
                const float ua0 = a0 / kTileW, ua1 = a1 / kTileW;
                const float ramp0 = stairRampY(f, a0), ramp1 = stairRampY(f, a1);
                if (!span.curb) {
                    const float yk = ramp0 + f.dy, yk1 = ramp1 + f.dy;
                    stepShade = SH_TREAD;
                    quad({ a0, yk, c0, u0, Vnose }, { a1, yk, c0, u0, 0.0f }, { a1, yk, c1, u1, 0.0f },
                         { a0, yk, c1, u1, Vnose }, nUp);
                    // The top tread meets the upper ground, so the last step has no riser.
                    if (k + 1 < f.N) {
                        stepShade = SH_RISER;
                        quad({ a1, yk, c0, u0, 1.0f }, { a1, yk1, c0, u0, Vnose }, { a1, yk1, c1, u1, Vnose },
                             { a1, yk, c1, u1, 1.0f }, nDn);
                    }
                    // At the flight's edge the wedge between step and ramp is closed; inside, a curb is.
                    stepShade = SH_SIDE;
                    if (edgeLo)
                        tri({ a0, ramp0, c0, ua0, 1.0f }, { a1, yk, c0, ua1, Vnose }, { a0, yk, c0, ua0, Vnose },
                            nCmin);
                    if (edgeHi)
                        tri({ a0, ramp0, c1, ua0, 1.0f }, { a0, yk, c1, ua0, Vnose }, { a1, yk, c1, ua1, Vnose },
                            nCmax);
                } else {
                    curbFace = true;
                    const float top0 = stairCurbTop(f, a0), top1 = stairCurbTop(f, a1);
                    stepShade = SH_TREAD;
                    quad({ a0, top0, c0, u0, Vnose }, { a1, top1, c0, u0, 0.0f }, { a1, top1, c1, u1, 0.0f },
                         { a0, top0, c1, u1, Vnose }, nUp);
                    stepShade = SH_SIDE;
                    // Outside the flight the wall stands on the ramp; beside steps it rises above their tread.
                    if (edgeLo) {
                        quad({ a0, ramp0, c0, ua0, 1.0f }, { a1, ramp1, c0, ua1, 1.0f }, { a1, top1, c0, ua1, Vnose },
                             { a0, top0, c0, ua0, Vnose }, nCmin);
                    } else if (top1 > top0 + 0.01f) {
                        tri({ a0, top0, c0, ua0, Vnose }, { a1, top0, c0, ua1, Vnose }, { a1, top1, c0, ua1, 0.0f },
                            nCmin);
                    }
                    if (edgeHi) {
                        quad({ a0, ramp0, c1, ua0, 1.0f }, { a1, ramp1, c1, ua1, 1.0f }, { a1, top1, c1, ua1, Vnose },
                             { a0, top0, c1, ua0, Vnose }, nCmax);
                    } else if (top1 > top0 + 0.01f) {
                        tri({ a0, top0, c1, ua0, Vnose }, { a1, top0, c1, ua1, Vnose }, { a1, top1, c1, ua1, 0.0f },
                            nCmax);
                    }
                    curbFace = false;
                }
            }
            // The first step's front, from its top down to the ground: risers stand at the back of treads.
            stepShade = SH_RISER;
            curbFace = span.curb;
            quad({ f.amin, f.ymin, c0, u0, 1.0f }, { f.amin, f.ymin + f.dy, c0, u0, Vnose },
                 { f.amin, f.ymin + f.dy, c1, u1, Vnose }, { f.amin, f.ymin, c1, u1, 1.0f }, nDn);
            curbFace = false;
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
