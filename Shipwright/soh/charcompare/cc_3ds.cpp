// charcompare — 3DS (OoT3D) viewport. See cc_3ds.h.
#include "cc_3ds.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "asset/ctr_rom.h"
#include "asset/zar.h"
#include "asset/cmb.h"
#include "asset/csab.h"

#include <fast/interpreter.h> // SCREEN_WIDTH / SCREEN_HEIGHT

// soh3d model bridge (soh3d_model.cpp, compiled into the tool). Pure C++/engine-agnostic.
extern "C" int SoH3D_AutoModelId(const char* zarPath);
extern "C" void SoH3D_EnsureModelProvider(void);
extern "C" void SoH3D_UpdateAnim(int modelId, const char* animName, float frame);

namespace cc {

static std::unique_ptr<SoH3D::CtrRom> g_rom;
static std::unordered_map<std::string, Model3ds> g_cache;

bool InitRom(std::string& err) {
    if (g_rom && g_rom->ok()) return true;
    const char* path = getenv("SOH3D_3DS_ROM");
    if (!path || !*path) {
        err = "SOH3D_3DS_ROM not set — point it at the decrypted OoT3D .3ds";
        return false;
    }
    g_rom = std::make_unique<SoH3D::CtrRom>(path);
    if (!g_rom->ok()) {
        err = "CtrRom: " + g_rom->error();
        g_rom.reset();
        return false;
    }
    return true;
}

Model3ds Load(const std::string& zarPath) {
    auto it = g_cache.find(zarPath);
    if (it != g_cache.end()) return it->second;

    Model3ds m;
    m.zarPath = zarPath;
    std::string err;
    if (!InitRom(err)) { m.error = err; return m; }

    auto zb = g_rom->read(zarPath);
    if (zb.empty()) { m.error = "ZAR not found: " + zarPath; return m; }
    SoH3D::Zar zar(std::move(zb));
    if (!zar.ok()) { m.error = "Zar: " + zar.error(); return m; }
    const SoH3D::ZarFile* cf = zar.firstWithSuffix(".cmb");
    if (!cf) { m.error = "no .cmb in " + zarPath; return m; }
    SoH3D::Cmb cmb(zar.read(*cf));
    if (!cmb.ok()) { m.error = "Cmb: " + cmb.error(); return m; }

    // Bind-pose bbox for auto-fit framing.
    auto groups = cmb.buildDrawGroups();
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (const auto& g : groups)
        for (const auto& v : g.verts)
            for (int k = 0; k < 3; k++) {
                lo[k] = std::min(lo[k], v.pos[k]);
                hi[k] = std::max(hi[k], v.pos[k]);
            }
    for (int k = 0; k < 3; k++) { m.bboxLo[k] = lo[k]; m.bboxHi[k] = hi[k]; }

    // Enumerate animations (CSAB base names).
    for (const auto& f : zar.files()) {
        const std::string& n = f.name;
        if (n.size() < 5 || n.compare(n.size() - 5, 5, ".csab") != 0) continue;
        std::string base = n;
        if (base.rfind("Anim/", 0) == 0) base = base.substr(5);
        base = base.substr(0, base.size() - 5); // strip .csab
        m.anims.push_back(base);
        // Record the animation's frame count so the GUI can bound/wrap playback to its true length.
        SoH3D::Csab csab(zar.read(f));
        m.animLen[base] = csab.ok() ? csab.duration() : 0;
    }

    // Register with the soh3d bridge for rendering (lazy-loads its own copy on first draw).
    SoH3D_EnsureModelProvider();
    m.modelId = SoH3D_AutoModelId(zarPath.c_str());
    m.ok = (m.modelId >= 0);
    if (!m.ok) m.error = "SoH3D_AutoModelId failed";

    fprintf(stderr, "[cc_3ds] loaded %s: id=%d bbox x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f] %zu anims\n",
            zarPath.c_str(), m.modelId, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], m.anims.size());

    g_cache[zarPath] = m;
    return m;
}

void SetAnim(const Model3ds& m, const std::string& animName, float frame) {
    if (!m.ok) return;
    SoH3D_UpdateAnim(m.modelId, animName.empty() ? "" : animName.c_str(), frame);
}

int AnimLength(const Model3ds& m, const std::string& csabBase) {
    if (csabBase.empty()) return 0;
    auto it = m.animLen.find(csabBase);
    return (it != m.animLen.end()) ? it->second : 0;
}

void EmitDlist(const Model3ds& m, std::vector<Gfx>& dl, std::unordered_map<Mtx*, MtxF>& mtx, DlistKeys& keys,
               float rx, float ry, float rz, const Rect& vp) {
    if (!m.ok) return;

    float ctr[3], ext[3];
    for (int k = 0; k < 3; k++) {
        ctr[k] = (m.bboxLo[k] + m.bboxHi[k]) * 0.5f;
        ext[k] = std::max((m.bboxHi[k] - m.bboxLo[k]) * 0.5f, 1.0f);
    }

    // Identity projection; modelview = Scale(fit) * R(rx,ry,rz) about the centre, into
    // ~80% NDC (mirrors dlist_harness BuildSoH3DDlist). Convention (GfxSpVertex):
    // clip_j = sum_k pos_k * MV[k][j] + MV[3][j], so MV[k][j] = S_j * R_{jk}.
    // xComp widens clip-X by (full width / viewport width) so a narrow (split) viewport
    // doesn't squash the model horizontally (the viewport maps clip[-1,1] to vp.w px).
    const float xComp = (float)SCREEN_WIDTH / vp.w;
    // 0.55 (was 0.8) leaves margin so the model isn't jammed against the viewport edges; CC_FIT3DS
    // overrides. (This is the GEOMETRY bbox, so it's a true fit unlike the N64 joint-bbox side.)
    static float fitTarget = [] { const char* e = getenv("CC_FIT3DS"); return e ? (float)atof(e) : 0.55f; }();
    // Frame by HEIGHT (Y), not max(width,height): a wide rest pose (e.g. Darunia's arms-out T-pose
    // bind) would otherwise shrink the model to fit its width. Height is pose-stable and is the
    // natural sizing dimension for a standing character, and matches the N64 side (also height-fit),
    // so the two halves are the same size. ext clamped so a flat model doesn't divide by ~0.
    const float fit = fitTarget / std::max(ext[1], 1.0f);
    const float fitZ = (fitTarget * 0.5f) / ext[2];
    // X is NEGATED so this modelview has a NEGATIVE determinant, matching the GAME's handedness.
    // Root cause (same as the N64 cc_n64.cpp X-flip): in the live game the SoH3D draw is submitted
    // with the SAME camera MP_matrix as the N64 Fast3D path (interpreter.cpp gfx_soh3d_draw_handler
    // passes mRsp->MP_matrix), whose 3x3 determinant is NEGATIVE (guPerspective handedness). This
    // hand-built modelview was a pure rotation+scale (det>0), so the 3DS half rendered MIRRORED vs
    // the game — and thus rotated OPPOSITE the (game-matched, det<0) N64 half. Negating clip-X makes
    // this MP det negative too, so both charcompare halves rotate the same way AND match in-game.
    // (SoH3D_GL disables backface culling, so the sign flip can't render the model inside-out.)
    const float S[3] = { -fit * xComp, fit, fitZ };
    auto rad = [](float d) { return d * 3.14159265358979f / 180.0f; };
    float cx = cosf(rad(rx)), sx = sinf(rad(rx)), cyr = cosf(rad(ry)), syr = sinf(rad(ry)), cz = cosf(rad(rz)),
          sz = sinf(rad(rz));
    float Rx[3][3] = { { 1, 0, 0 }, { 0, cx, -sx }, { 0, sx, cx } };
    float Ry[3][3] = { { cyr, 0, syr }, { 0, 1, 0 }, { -syr, 0, cyr } };
    float Rz[3][3] = { { cz, -sz, 0 }, { sz, cz, 0 }, { 0, 0, 1 } };
    auto mul3 = [](const float A[3][3], const float B[3][3], float O[3][3]) {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                O[i][j] = 0;
                for (int k = 0; k < 3; k++) O[i][j] += A[i][k] * B[k][j];
            }
    };
    float Rzy[3][3], R[3][3];
    mul3(Rz, Ry, Rzy);
    mul3(Rzy, Rx, R); // R = Rz*Ry*Rx

    keys.mtxStore.push_back(std::make_unique<Mtx>());
    keys.mtxStore.push_back(std::make_unique<Mtx>());
    Mtx* projKey = keys.mtxStore[keys.mtxStore.size() - 2].get();
    Mtx* mvKey = keys.mtxStore[keys.mtxStore.size() - 1].get();

    MtxF& proj = mtx[projKey];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) proj.mf[i][j] = (i == j) ? 1.0f : 0.0f;

    MtxF& mv = mtx[mvKey];
    memset(mv.mf, 0, sizeof(mv.mf));
    const float bias[3] = { 0.0f, 0.0f, 0.5f };
    for (int j = 0; j < 3; j++) {
        for (int k = 0; k < 3; k++) mv.mf[k][j] = S[j] * R[j][k];
        float t = bias[j];
        for (int k = 0; k < 3; k++) t -= mv.mf[k][j] * ctr[k];
        mv.mf[3][j] = t;
    }
    mv.mf[3][3] = 1.0f;

    keys.vpStore.push_back(std::make_unique<Vp>());
    Vp* vpp = keys.vpStore.back().get();
    vpp->vp.vscale[0] = (vp.w / 2) * 4;
    vpp->vp.vscale[1] = (vp.h / 2) * 4;
    vpp->vp.vscale[2] = G_MAXZ;
    vpp->vp.vscale[3] = 0;
    vpp->vp.vtrans[0] = (vp.x0 + vp.w / 2) * 4;
    vpp->vp.vtrans[1] = (vp.y0 + vp.h / 2) * 4;
    vpp->vp.vtrans[2] = 0;
    vpp->vp.vtrans[3] = 0;

    { Gfx g = gsSPViewport(vpp); dl.push_back(g); }
    { Gfx g = gsDPSetScissor(G_SC_NON_INTERLACE, vp.x0, vp.y0, vp.x0 + vp.w, vp.y0 + vp.h); dl.push_back(g); }
    { Gfx g = gsSPMatrix(projKey, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION); dl.push_back(g); }
    { Gfx g = gsSPMatrix(mvKey, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW); dl.push_back(g); }
    // High bit of the handle = "lit" (character half-Lambert form term).
    { Gfx g; gSPSoH3DDraw(&g, m.modelId | (int)0x80000000, 255, 255, 255); dl.push_back(g); }
    { Gfx g; gSPSoH3DRenderPass(&g); dl.push_back(g); }
}

} // namespace cc
