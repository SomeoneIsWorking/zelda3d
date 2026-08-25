// mm3d_collision — see mm3d_collision.h.
//
// The MM3D collision layout is parsed by the SHARED asset/zcol parser (it version-gates OoT3D vs
// MM3D off the ZSI magic; the MM3D layout was derived and verified in
// tools/zelda3d_collision_layout.cpp + tools/zelda3d_collision_test.cpp — 110/111 scenes at 100%
// plane identity / 99.9% face-normal agreement). This module only converts that into MM's runtime
// CollisionHeader and hands it to BgCheck.
//
// MM3D scene coordinates are IDENTICAL to N64 coordinates — verified by matching the room ZSI's
// actor list against the live N64 actor list (see debug_journal/2026-07-21-mm-scene-room-pipeline.md).
// So NO placement transform is applied here, deliberately.
#include "mm3d_collision.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "asset/ctr_rom.h"
#include "asset/lzs.h"
#include "asset/zcol.h"

#include "mm3d_draw.h" // Zelda3D_MM_SceneName — one owner for the sceneNum -> MM3D name table
#include "mm3d_model_store.h"

namespace {

// Freed on the next build; BgCheck holds pointers into these for the lifetime of the scene.
CollisionHeader* sHeader = nullptr;
Vec3s* sVtx = nullptr;
CollisionPoly* sPoly = nullptr;
SurfaceType* sSurf = nullptr;

void freePrevious() {
    free(sHeader);
    free(sVtx);
    free(sPoly);
    free(sSurf);
    sHeader = nullptr;
    sVtx = nullptr;
    sPoly = nullptr;
    sSurf = nullptr;
}

// OPT-IN (ZELDA3D_MM_COLLISION=1) and OFF BY DEFAULT — this HANGS on some scenes.
//
// The parse and the geometry are correct (Clock Town verified: Link lands exactly on the 3DS floor,
// and scene exits fire — walking the south gate transitioned to Termina Field). But entering a LARGE
// scene hangs the game on load, spinning forever inside StaticLookup_AddPolyToSSList <-
// BgCheck_InitStaticLookup <- BgCheck_Allocate (gdb backtrace, reproduced on z2_00keikoku /
// Termina Field: 2914 verts, 4503 polys vs the N64 mesh's 1265/1685).
//
// RULED OUT: the per-scene hand-tuned node cap. sSceneSubdivisionList's nodeListMax is sized for the
// N64 mesh, and z_bgcheck applies it WITHOUT an overflow check (its own comment says so), so it was
// the obvious suspect; bypassing it for diverted headers (Zelda3D_MM_CollisionDiverted) did NOT fix
// the hang. The cause is elsewhere in the static-lookup build — most likely a poly whose subdivision
// bounds explode, or an SS list driven circular.
//
// A hang on scene entry is worse than N64 collision, so this stays off until that is understood.
// Do NOT default it back on without reproducing a clean Termina Field load.
int collisionDivertEnabled() {
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("ZELDA3D_MM_COLLISION");
        v = (e != nullptr && e[0] != '\0') ? atoi(e) : 0;
    }
    return v;
}

// Set while the header handed to BgCheck is OURS. z_bgcheck's per-scene subdivision/node-pool
// constants (sSceneSubdivisionList) are hand-tuned to the N64 mesh, so they do not apply to it.
int sDiverted = 0;

} // namespace

extern "C" int Zelda3D_MM_CollisionDiverted(void) {
    return sDiverted;
}

extern "C" CollisionHeader* Zelda3D_MM_BuildSceneCollision(PlayState* play, CollisionHeader* n64) {
    sDiverted = 0;
    if (play == nullptr || !collisionDivertEnabled()) {
        return nullptr;
    }
    const char* sceneName = Zelda3D_MM_SceneName(play);
    if (sceneName == nullptr) {
        return nullptr; // no MM3D scene -> N64 collision
    }
    Zelda3D::CtrRom* rom = Zelda3D_MM_Rom();
    if (rom == nullptr) {
        return nullptr;
    }
    const std::string path = std::string("/scenes/") + sceneName + "_info.zsi";
    std::vector<uint8_t> bytes = rom->read(path);
    if (bytes.empty()) {
        return nullptr;
    }
    // MM3D scene ZSIs are LzS-compressed; the room path discovered this the hard way.
    if (Zelda3D::LzsIsCompressed(bytes)) {
        std::string err;
        std::vector<uint8_t> inflated = Zelda3D::LzsDecompress(bytes, &err);
        if (inflated.empty()) {
            fprintf(stderr, "[MM3D-COL] %s: LzS inflate failed: %s\n", path.c_str(), err.c_str());
            return nullptr;
        }
        bytes = std::move(inflated);
    }
    Zelda3D::OoT3DCollision col(bytes);
    if (!col.ok()) {
        fprintf(stderr, "[MM3D-COL] %s: %s\n", path.c_str(), col.error().c_str());
        return nullptr;
    }
    const std::vector<Zelda3D::OoT3DCollision::Vert>& V = col.verts();
    const std::vector<Zelda3D::OoT3DCollision::Poly>& P = col.polys();
    const std::vector<Zelda3D::OoT3DCollision::Surface>& S = col.surfaces();
    if (V.empty() || P.empty()) {
        return nullptr;
    }
    // CollisionPoly stores 13-bit vertex indices, so a scene past 8192 verts cannot be expressed.
    // Report and fall back rather than silently truncating into wrong geometry.
    if (V.size() >= 8192 || P.size() >= 65535) {
        fprintf(stderr,
                "[MM3D-COL] %s: %zu verts / %zu polys exceeds the collision index budget — "
                "using N64 collision for this scene\n",
                path.c_str(), V.size(), P.size());
        return nullptr;
    }

    freePrevious();

    const size_t nSurf = S.empty() ? 1 : S.size();
    sHeader = (CollisionHeader*)calloc(1, sizeof(CollisionHeader));
    sVtx = (Vec3s*)malloc(sizeof(Vec3s) * V.size());
    sPoly = (CollisionPoly*)calloc(P.size(), sizeof(CollisionPoly));
    sSurf = (SurfaceType*)calloc(nSurf, sizeof(SurfaceType));
    if (sHeader == nullptr || sVtx == nullptr || sPoly == nullptr || sSurf == nullptr) {
        freePrevious();
        return nullptr;
    }

    s16 lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
    for (size_t i = 0; i < V.size(); i++) {
        sVtx[i].x = V[i].x;
        sVtx[i].y = V[i].y;
        sVtx[i].z = V[i].z;
        const s16 c[3] = { V[i].x, V[i].y, V[i].z };
        for (int k = 0; k < 3; k++) {
            if (i == 0 || c[k] < lo[k])
                lo[k] = c[k];
            if (i == 0 || c[k] > hi[k])
                hi[k] = c[k];
        }
    }
    for (size_t k = 0; k < P.size(); k++) {
        // The parser already masks vertex indices to 13 bits; the upper bits of flags_vIA/vIB are
        // MM's poly-exclusion/conveyor flags, which the MM3D record does not carry, so they stay 0.
        sPoly[k].type = P[k].type;
        sPoly[k].flags_vIA = P[k].vA;
        sPoly[k].flags_vIB = P[k].vB;
        sPoly[k].vIC = P[k].vC;
        sPoly[k].normal.x = P[k].nx;
        sPoly[k].normal.y = P[k].ny;
        sPoly[k].normal.z = P[k].nz;
        // Runtime dist is s16; the file stores it as f32. Round rather than truncate so the plane
        // stays as close to the authored one as the runtime type allows.
        sPoly[k].dist = (s16)lroundf(P[k].dist);
    }
    for (size_t i = 0; i < S.size(); i++) {
        sSurf[i].data[0] = S[i].data0;
        sSurf[i].data[1] = S[i].data1;
    }

    sHeader->minBounds.x = lo[0];
    sHeader->minBounds.y = lo[1];
    sHeader->minBounds.z = lo[2];
    sHeader->maxBounds.x = hi[0];
    sHeader->maxBounds.y = hi[1];
    sHeader->maxBounds.z = hi[2];
    sHeader->numVertices = (u16)V.size();
    sHeader->vtxList = sVtx;
    sHeader->numPolygons = (u16)P.size();
    sHeader->polyList = sPoly;
    sHeader->surfaceTypeList = sSurf;
    // Water boxes and bg-camera regions are gameplay metadata the N64 header already resolves; carry
    // them over rather than reinterpreting the MM3D file's own lists, so swimming and camera regions
    // behave exactly as before while only the GEOMETRY changes.
    if (n64 != nullptr) {
        sHeader->bgCamList = n64->bgCamList;
        sHeader->numWaterBoxes = n64->numWaterBoxes;
        sHeader->waterBoxes = n64->waterBoxes;
    }

    sDiverted = 1;
    fprintf(stderr, "[MM3D-COL] %s: %zu verts, %zu polys, %zu surface types (N64 was %u/%u)\n", path.c_str(), V.size(),
            P.size(), S.size(), n64 ? n64->numVertices : 0, n64 ? n64->numPolygons : 0);
    return sHeader;
}
