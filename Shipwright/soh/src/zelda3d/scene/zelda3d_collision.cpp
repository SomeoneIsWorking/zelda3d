// OoT3D scene-collision subsystem: drive gameplay (BgCheck floors/walls) from the parsed OoT3D
// scene collision mesh instead of the N64 one, so Link physically walks the OoT3D world. Extracted
// from zelda3d.c (Phase 2 codebase reorg — see this repo's docs/codemap.md). Zelda3D_BuildSceneCollision
// is the single public entry point (declared in zelda3d.h, called from Scene_CommandCollisionHeader);
// everything else here is a file-local helper.
#include "../core/zelda3d_runtime.h"
#include "scene_draw.h"
#include "gameplay_collision.h" // extern "C" declarations for Zelda3D_BuildSceneCollision / Zelda3D_CollisionEnabled
#include "zelda3d_collision.h" // C-ABI bridge: Zelda3D_RawCollision / LoadSceneCollisionRaw / stair treads
#include <stdlib.h> // getenv, malloc/calloc/free
#include <stdio.h>  // fprintf/stderr
#include <string.h> // memcpy
#include <math.h>   // lrintf

// --- OoT3D collision: drive gameplay (BgCheck floors/walls) from the OoT3D scene collision
// mesh so Link physically walks the OoT3D world (see PROGRESS.md "USE OoT3D COLLISION"). The
// render mesh and the collision are then ONE geometry — fixes both floor height AND walls,
// which no render-side Y-offset can. Zelda3D_BuildSceneCollision converts the parsed OoT3D
// collision into a SoH CollisionHeader; Scene_CommandCollisionHeader installs it instead of
// the N64 one. Gate: Zelda3D_Enabled() + env ZELDA3D_COLLISION (default ON; =0 for A/B) + REPL
// `collision` (takes effect on next scene load / warp). ---
int gZelda3dCollision = 1;

int Zelda3D_CollisionEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("ZELDA3D_COLLISION");
        cached = (v != NULL && v[0] == '0') ? 0 : 1; // default ON
    }
    return Zelda3D_Enabled() && cached && gZelda3dCollision;
}

// Build a SoH CollisionHeader from the current scene's OoT3D collision, or NULL when
// disabled / unavailable (caller then uses the N64 collision). The header + its arrays are
// malloc'd and kept resident for the scene lifetime; the previous build is freed here, so a
// scene change / warp recycles it (BgCheck_Allocate stores the pointer and references the
// arrays, so they must outlive the call). Verts are N64-unit world-space (same frame as the
// render mesh), so no transform — direct copy. One generic SurfaceType (plain ground) backs
// all polys; floor/wall/ceiling classification comes from each poly's normal, not the type.
// Find the N64 floor poly under world (x,y,z) and return its SurfaceType.data[0]; the low 13
// bits hold the camera-region index (0x00FF) + the scene-EXIT index (0x1F00). Manual
// point-in-triangle over the N64 header (BgCheck isn't wired to it at build time), choosing the
// floor whose plane Y is closest to y (multi-level scenes). Returns 1 on hit.
// Why: SoH places Link using the N64 spawn list, which is authored against the N64 EXIT layout.
// OoT3D's own collision puts exit triangles at different XZ extents, so an N64 spawn point can
// land on an OoT3D exit poly and bounce Link straight back out (the Kakariko-graveyard "spawns
// on the leave trigger" bug). Re-sourcing exit+cam from the N64 floor makes exits/cameras fire
// at the SAME world places as vanilla, matching the spawn points.
static int Zelda3D_N64FloorData0(CollisionHeader* n64, float x, float y, float z, u32* outData0) {
    int i, best = -1;
    float bestDy = 1e9f;
    if (n64 == NULL || n64->polyList == NULL || n64->vtxList == NULL ||
        n64->surfaceTypeList == NULL) {
        return 0;
    }
    for (i = 0; i < n64->numPolygons; i++) {
        CollisionPoly* p = &n64->polyList[i];
        float ny = COLPOLY_GET_NORMAL(p->normal.y);
        Vec3s *a, *b, *c;
        float ax, az, bx, bz, cx, cz, d1, d2, d3, planeY, dy;
        if (ny < 0.5f) {
            continue; // floors only
        }
        a = &n64->vtxList[p->flags_vIA & 0x1FFF];
        b = &n64->vtxList[p->flags_vIB & 0x1FFF];
        c = &n64->vtxList[p->vIC & 0x1FFF];
        ax = a->x; az = a->z; bx = b->x; bz = b->z; cx = c->x; cz = c->z;
        d1 = (x - bx) * (az - bz) - (ax - bx) * (z - bz);
        d2 = (x - cx) * (bz - cz) - (bx - cx) * (z - cz);
        d3 = (x - ax) * (cz - az) - (cx - ax) * (z - az);
        if (((d1 < 0) || (d2 < 0) || (d3 < 0)) && ((d1 > 0) || (d2 > 0) || (d3 > 0))) {
            continue; // (x,z) not inside this triangle
        }
        planeY = -(COLPOLY_GET_NORMAL(p->normal.x) * x + COLPOLY_GET_NORMAL(p->normal.z) * z +
                   (float)p->dist) / ny;
        dy = planeY - y;
        if (dy < 0.0f) {
            dy = -dy;
        }
        if (dy < bestDy) {
            bestDy = dy;
            best = i;
        }
    }
    if (best < 0) {
        return 0;
    }
    *outData0 = n64->surfaceTypeList[n64->polyList[best].type].data[0];
    return 1;
}

// #25 climb fix: find the N64 WALL poly matching an OoT3D wall triangle and return its SurfaceType
// data[0]. Mirrors Zelda3D_N64FloorData0, but for vertical surfaces: the wall-climb property (data0
// bits 21..25 → func_80041D94 → climb flags) is GAMEPLAY data and must come from the authoritative
// N64 collision, NOT the OoT3D mesh — whose per-triangle SurfaceType is unreliable (e.g. Kokiri well
// wall: N64 has both quad triangles climbable, OoT3D leaves the UPPER triangle wallProp=0, so Link
// loses wall contact and drops at the diagonal seam ~halfway up — every climbable). Match: same
// horizontal normal (dot > 0.85), the OoT3D centroid projects INTO the N64 wall triangle (in the
// wall's dominant vertical plane, so tessellation offsets don't matter), nearest perpendicular plane.
static int Zelda3D_N64WallData0(CollisionHeader* n64, float cx, float cy, float cz, float onx, float onz,
                              u32* outData0) {
    int i, best = -1;
    float bestPlane = 1e9f;
    if (n64 == NULL || n64->polyList == NULL || n64->vtxList == NULL ||
        n64->surfaceTypeList == NULL) {
        return 0;
    }
    for (i = 0; i < n64->numPolygons; i++) {
        CollisionPoly* p = &n64->polyList[i];
        float nx = COLPOLY_GET_NORMAL(p->normal.x);
        float ny = COLPOLY_GET_NORMAL(p->normal.y);
        float nz = COLPOLY_GET_NORMAL(p->normal.z);
        Vec3s *a, *b, *c;
        float au, av, bu, bv, cu, cv, pu, pv, d1, d2, d3, planeDist;
        int useX;
        if (ny > 0.5f || ny < -0.5f) {
            continue; // walls only
        }
        if ((nx * onx + nz * onz) < 0.85f) {
            continue; // must face the same horizontal direction
        }
        a = &n64->vtxList[p->flags_vIA & 0x1FFF];
        b = &n64->vtxList[p->flags_vIB & 0x1FFF];
        c = &n64->vtxList[p->vIC & 0x1FFF];
        // Project to the wall's dominant vertical plane: (X,Y) for a ±Z wall, (Z,Y) for a ±X wall.
        useX = (nz * nz >= nx * nx);
        au = useX ? a->x : a->z; av = a->y;
        bu = useX ? b->x : b->z; bv = b->y;
        cu = useX ? c->x : c->z; cv = c->y;
        pu = useX ? cx : cz;     pv = cy;
        d1 = (pu - bu) * (av - bv) - (au - bu) * (pv - bv);
        d2 = (pu - cu) * (bv - cv) - (bu - cu) * (pv - cv);
        d3 = (pu - au) * (cv - av) - (cu - au) * (pv - av);
        if (((d1 < 0) || (d2 < 0) || (d3 < 0)) && ((d1 > 0) || (d2 > 0) || (d3 > 0))) {
            continue; // centroid not inside this wall triangle's vertical profile
        }
        planeDist = nx * cx + ny * cy + nz * cz + (float)p->dist;
        if (planeDist < 0.0f) {
            planeDist = -planeDist;
        }
        if (planeDist < bestPlane) {
            bestPlane = planeDist;
            best = i;
        }
    }
    if (best < 0 || bestPlane > 50.0f) {
        return 0;
    }
    *outData0 = n64->surfaceTypeList[n64->polyList[best].type].data[0];
    return 1;
}

// #74 climb-bit propagation across a tessellated quad. The N64 re-source above matches per-triangle,
// but OoT3D splits a climbable wall quad into two triangles and the centroid-inside test can match
// one while its sibling's centroid lands just outside the N64 triangle profile near the shared
// diagonal -> one triangle climbable, the other not, so Link's wall-contact raycast hits the dead
// triangle and he dismounts mid-climb (Hyrule Castle ladder: idx 162 flags=8, idx 163 flags=0;
// #74/#25/#79). Heal it: a wall triangle that shares an EDGE (two vertex indices) with a climbable,
// CO-PLANAR wall triangle is the same physical surface, so it inherits the climb bits (data0 bits
// 21..25). Edge-sharing + same plane keeps this from leaking a climb bit onto an unrelated wall.
static int Zelda3D_WallSharesEdge(CollisionPoly* a, CollisionPoly* b) {
    s16 av[3] = { (s16)(a->flags_vIA & 0x1FFF), (s16)(a->flags_vIB & 0x1FFF), (s16)a->vIC };
    s16 bv[3] = { (s16)(b->flags_vIA & 0x1FFF), (s16)(b->flags_vIB & 0x1FFF), (s16)b->vIC };
    int i, j, shared = 0;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            if (av[i] == bv[j]) {
                shared++;
                break;
            }
        }
    }
    return shared >= 2;
}

static void Zelda3D_PropagateWallClimbBits(CollisionPoly* poly, int numPolys, SurfaceType* types) {
    const u32 kClimb = 0x03E00000u; // wall-climb property, data0 bits 21..25
    int i, j, changed = 1, pass = 0;
    // Iterate to flow the bit across a chain of triangles (a tall ladder is several stacked quads).
    while (changed && pass < 8) {
        changed = 0;
        pass++;
        for (i = 0; i < numPolys; i++) {
            float niy = COLPOLY_GET_NORMAL(poly[i].normal.y);
            if (niy > 0.5f || niy < -0.5f) {
                continue; // walls only
            }
            if ((types[poly[i].type].data[0] & kClimb) != 0) {
                continue; // already climbable
            }
            for (j = 0; j < numPolys; j++) {
                if (j == i) {
                    continue;
                }
                if ((types[poly[j].type].data[0] & kClimb) == 0) {
                    continue; // source must be climbable
                }
                // Same plane: same normal direction (dot ~1) and same signed plane offset.
                if (COLPOLY_GET_NORMAL(poly[i].normal.x) * COLPOLY_GET_NORMAL(poly[j].normal.x) +
                        COLPOLY_GET_NORMAL(poly[i].normal.y) * COLPOLY_GET_NORMAL(poly[j].normal.y) +
                        COLPOLY_GET_NORMAL(poly[i].normal.z) * COLPOLY_GET_NORMAL(poly[j].normal.z) <
                    0.985f) {
                    continue;
                }
                if (abs((int)poly[i].dist - (int)poly[j].dist) > 8) {
                    continue;
                }
                if (!Zelda3D_WallSharesEdge(&poly[i], &poly[j])) {
                    continue;
                }
                types[poly[i].type].data[0] =
                    (types[poly[i].type].data[0] & ~kClimb) | (types[poly[j].type].data[0] & kClimb);
                changed = 1;
                break;
            }
        }
    }
}

// #5 — find the freshly-built OoT3D base floor poly under world (x,z) with plane Y closest to y
// (mirrors Zelda3D_N64FloorData0 but over the in-build vtx/poly arrays). Returns the poly index
// (== its own SurfaceType slot, since each poly indexes type=i) or -1. Used to give a generated
// stair tread the SAME surface type (floor material + already-N64-sourced exit/cam) as the kaidan
// ramp it sits on.
static int Zelda3D_BaseFloorPoly(Vec3s* vtx, CollisionPoly* poly, int numPolys,
                               float x, float y, float z) {
    int i, best = -1;
    float bestDy = 1e9f;
    for (i = 0; i < numPolys; i++) {
        CollisionPoly* p = &poly[i];
        float ny = COLPOLY_GET_NORMAL(p->normal.y);
        Vec3s *a, *b, *c;
        float ax, az, bx, bz, cx, cz, d1, d2, d3, planeY, dy;
        if (ny < 0.5f) {
            continue; // floors only
        }
        a = &vtx[p->flags_vIA & 0x1FFF];
        b = &vtx[p->flags_vIB & 0x1FFF];
        c = &vtx[p->vIC & 0x1FFF];
        ax = a->x; az = a->z; bx = b->x; bz = b->z; cx = c->x; cz = c->z;
        d1 = (x - bx) * (az - bz) - (ax - bx) * (z - bz);
        d2 = (x - cx) * (bz - cz) - (bx - cx) * (z - cz);
        d3 = (x - ax) * (cz - az) - (cx - ax) * (z - az);
        if (((d1 < 0) || (d2 < 0) || (d3 < 0)) && ((d1 > 0) || (d2 > 0) || (d3 > 0))) {
            continue;
        }
        planeY = -(COLPOLY_GET_NORMAL(p->normal.x) * x + COLPOLY_GET_NORMAL(p->normal.z) * z +
                   (float)p->dist) / ny;
        dy = planeY - y;
        if (dy < 0.0f) {
            dy = -dy;
        }
        if (dy < bestDy) {
            bestDy = dy;
            best = i;
        }
    }
    return best;
}

CollisionHeader* Zelda3D_BuildSceneCollision(PlayState* play, CollisionHeader* n64) {
    static CollisionHeader* sHeader = NULL;
    static SurfaceType* sSurfaceTypes = NULL;
    static CamData* sCamData = NULL;
    static WaterBox* sWaterBoxes = NULL;
    const char* sceneName;
    Zelda3D_RawCollision raw;
    CollisionHeader* h;
    Vec3s* vtx;
    CollisionPoly* poly;
    int i;
    int nSurf;         // surfaceType entries allocated (>=1)
    s16 minX, minY, minZ, maxX, maxY, maxZ;
    size_t camLen = 1; // entries in sCamData (>=1: a dummy when no N64 list)

    if (play == NULL || !Zelda3D_CollisionEnabled()) {
        return NULL;
    }
    sceneName = Zelda3D_SceneName(play);
    if (sceneName == NULL) {
        return NULL; // scene has no OoT3D mapping -> N64 collision
    }
    if (!Zelda3D_LoadSceneCollisionRaw(sceneName, &raw)) {
        return NULL;
    }

    // #5 — stepped stairs: collect the kaidan tread quads (world-space) for this scene, to splice
    // in as stepped floor polys so Link grounds on the visible steps (not the smooth ramp). Each
    // quad is 2 tris. CollisionPoly stores 13-bit vertex indices, so the combined vertex count
    // must stay < 8192 (and the poly/u16 count < 65535); if a scene would overflow, drop the
    // stair collision (render steps still show — only the per-step grounding is skipped there).
    float* stairV = NULL;  // 3 floats per vert
    int stairNV = 0;
    int* stairT = NULL;    // 3 vtx-indices per tri
    int stairNT = 0;
    Zelda3D_CollectSceneStairTreads(sceneName, &stairV, &stairNV, &stairT, &stairNT);
    if (stairNT > 0 && (raw.numVerts + stairNV >= 8000 || raw.numPolys + stairNT >= 60000)) {
        fprintf(stderr, "[Zelda3D] stairs: %d verts + %d tread verts / %d polys + %d tread tris exceeds the "
               "collision index budget — skipping stepped stair collision for %s\n",
               raw.numVerts, stairNV, raw.numPolys, stairNT, sceneName);
        Zelda3D_FreeStairTreads(stairV, stairT);
        stairV = NULL; stairT = NULL; stairNV = 0; stairNT = 0;
    }

    // Free the previous scene's build (its arrays were referenced by the old colCtx, which is
    // being replaced now).
    if (sHeader != NULL) {
        free(sHeader->vtxList);
        free(sHeader->polyList);
        free(sHeader);
    }
    free(sSurfaceTypes);
    free(sCamData);
    free(sWaterBoxes);
    sCamData = NULL;
    sWaterBoxes = NULL;

    // One SurfaceType PER POLY (not the compact OoT3D list): each floor poly's exit+cam bits get
    // re-sourced from the N64 collision below, so they must be addressable individually. The
    // generated stair treads (stairNT tris over stairNV verts) extend all three arrays.
    int totalVerts = raw.numVerts + stairNV;
    int totalPolys = raw.numPolys + stairNT;
    nSurf = (totalPolys > 0) ? totalPolys : 1;
    h = (CollisionHeader*)calloc(1, sizeof(CollisionHeader));
    vtx = (Vec3s*)malloc(sizeof(Vec3s) * totalVerts);
    poly = (CollisionPoly*)calloc(totalPolys, sizeof(CollisionPoly));
    sSurfaceTypes = (SurfaceType*)calloc(nSurf, sizeof(SurfaceType));
    if (h == NULL || vtx == NULL || poly == NULL || sSurfaceTypes == NULL) {
        free(h); free(vtx); free(poly); free(sSurfaceTypes);
        sHeader = NULL; sSurfaceTypes = NULL;
        Zelda3D_FreeStairTreads(stairV, stairT);
        Zelda3D_FreeRawCollision(&raw);
        return NULL;
    }
    // SurfaceType.data[0]: low byte = camDataIndex (camera region), (data[0]>>8)&0x1F = scene EXIT
    // index, higher bits = floor/wall flags; data[1] = floor type / material. The OoT3D bitfield
    // layout matches N64 (same game), so floor-type/flag bits copy verbatim. But the cam + exit
    // INDICES point into the OoT3D scene's own camera/exit lists, whose triangles sit at different
    // XZ extents than N64's. SoH spawns Link at N64-authored coords, so trusting OoT3D's exit polys
    // drops Link onto a leave-trigger at spawn. Per floor poly (below) we splice the cam+exit (low
    // 13 bits) from the N64 floor at that location, so exits/cameras fire exactly where vanilla's
    // do. The per-poly init happens in the poly loop after vtx[] is built.

    // Waterboxes + camera REGION data are gameplay volumes actors index + DEREFERENCE (e.g.
    // Bg_Spot01_Idomizu writes waterBoxes[0].ySurface -> NULL crash if absent; the surfaceType cam
    // index selects cameraDataList[idx]). We have not REd those OoT3D sub-lists yet; copy them from
    // the N64 header (same world space + same indices since same game). The CamData entries keep
    // their N64 camPosData pointers (valid for the scene), so fixed/pivot cameras still work.
    if (n64 != NULL && n64->numWaterBoxes > 0 && n64->waterBoxes != NULL) {
        sWaterBoxes = (WaterBox*)malloc(sizeof(WaterBox) * n64->numWaterBoxes);
        if (sWaterBoxes != NULL) {
            memcpy(sWaterBoxes, n64->waterBoxes, sizeof(WaterBox) * n64->numWaterBoxes);
        }
    }
    if (n64 != NULL && n64->cameraDataList != NULL && n64->cameraDataListLen > 0) {
        sCamData = (CamData*)malloc(sizeof(CamData) * n64->cameraDataListLen);
        if (sCamData != NULL) {
            memcpy(sCamData, n64->cameraDataList, sizeof(CamData) * n64->cameraDataListLen);
            camLen = n64->cameraDataListLen;
        }
    }
    if (sCamData == NULL) {
        sCamData = (CamData*)calloc(1, sizeof(CamData)); // fallback: CAM_SET_NORMAL0 follow cam
        sCamData[0].cameraSType = CAM_SET_NORMAL0;
        camLen = 1;
    }

    minX = maxX = raw.verts[0]; minY = maxY = raw.verts[1]; minZ = maxZ = raw.verts[2];
    for (i = 0; i < raw.numVerts; i++) {
        s16 x = raw.verts[i * 3 + 0], y = raw.verts[i * 3 + 1], z = raw.verts[i * 3 + 2];
        vtx[i].x = x; vtx[i].y = y; vtx[i].z = z;
        if (x < minX) minX = x; if (x > maxX) maxX = x;
        if (y < minY) minY = y; if (y > maxY) maxY = y;
        if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
    }

    // #11 — flatten the short outward rim-bevels of RAISED WALKABLE patches so N64 BgCheck (ny>0.5
    // == floor) lets Link step onto them, the way OoT3D's player physics walks up such short curbs.
    // We run faithful N64 BgCheck (a poly with ny<=0.5 is a WALL regardless of how short it is) over
    // OoT3D geometry authored for 3DS step-up physics, so the ~20u-tall chamfered rim of a low patch
    // (e.g. the Kokiri clover patch ~(-460,183)) fences Link out where both originals let him walk.
    // Reclassify ONLY a short (<= ZELDA3D_LIP_MAX_H) upward-facing (0.15 < ny <= 0.5) bevel whose TOP
    // edge is shared with a real floor poly — i.e. it rims a walkable surface — so a standalone short
    // wall (a cliff-base bevel, whose top borders a WALL, not a floor) is left untouched. A qualifying
    // bevel becomes a horizontal floor at its top edge (same plane construction as the stair treads
    // below); the main loop then sees ny>0.5 and classifies it as floor + re-sources its cam/exit.
#define ZELDA3D_LIP_MAX_H 24
    {
        unsigned char* floorVtx = (unsigned char*)calloc((size_t)(raw.numVerts > 0 ? raw.numVerts : 1), 1);
        if (floorVtx != NULL) {
            int j;
            // Mark every vertex used by a real (ny>0.5) OoT3D floor poly.
            for (j = 0; j < raw.numPolys; j++) {
                if (COLPOLY_GET_NORMAL(raw.polyNrm[j * 3 + 1]) > 0.5f) {
                    floorVtx[raw.polyVtx[j * 3 + 0] & 0x1FFF] = 1;
                    floorVtx[raw.polyVtx[j * 3 + 1] & 0x1FFF] = 1;
                    floorVtx[raw.polyVtx[j * 3 + 2] & 0x1FFF] = 1;
                }
            }
            for (j = 0; j < raw.numPolys; j++) {
                float bny = COLPOLY_GET_NORMAL(raw.polyNrm[j * 3 + 1]);
                int ja, jb, jc;
                s16 ymin, ymax;
                if (bny <= 0.15f || bny > 0.5f) {
                    continue;
                }
                ja = raw.polyVtx[j * 3 + 0] & 0x1FFF;
                jb = raw.polyVtx[j * 3 + 1] & 0x1FFF;
                jc = raw.polyVtx[j * 3 + 2] & 0x1FFF;
                ymin = ymax = vtx[ja].y;
                if (vtx[jb].y < ymin) ymin = vtx[jb].y; if (vtx[jb].y > ymax) ymax = vtx[jb].y;
                if (vtx[jc].y < ymin) ymin = vtx[jc].y; if (vtx[jc].y > ymax) ymax = vtx[jc].y;
                if ((ymax - ymin) > ZELDA3D_LIP_MAX_H) {
                    continue; // tall slope -> a real wall/cliff, not a curb
                }
                // Only flatten if the TOP edge belongs to a real floor poly (rim of a walkable patch).
                if (((vtx[ja].y >= ymax - 2) && floorVtx[ja]) || ((vtx[jb].y >= ymax - 2) && floorVtx[jb]) ||
                    ((vtx[jc].y >= ymax - 2) && floorVtx[jc])) {
                    raw.polyNrm[j * 3 + 0] = 0;
                    raw.polyNrm[j * 3 + 1] = COLPOLY_SNORMAL(1.0f); // horizontal floor at the patch top
                    raw.polyNrm[j * 3 + 2] = 0;
                    raw.polyDist[j] = -(float)ymax; // n=(0,1,0): n.p + dist == 0 -> dist = -ymax
                }
            }
            free(floorVtx);
        }
    }
#undef ZELDA3D_LIP_MAX_H

    for (i = 0; i < raw.numPolys; i++) {
        u16 ty = raw.polyType[i];
        u32 d0 = (ty < (u16)raw.numSurf && raw.surf0 != NULL) ? raw.surf0[ty] : 0;
        u32 d1 = (ty < (u16)raw.numSurf && raw.surf1 != NULL) ? raw.surf1[ty] : 0;
        float ny;
        poly[i].type = i; // each poly indexes its OWN SurfaceType slot
        poly[i].flags_vIA = raw.polyVtx[i * 3 + 0] & 0x1FFF;
        poly[i].flags_vIB = raw.polyVtx[i * 3 + 1] & 0x1FFF;
        poly[i].vIC = raw.polyVtx[i * 3 + 2] & 0x1FFF;
        poly[i].normal.x = raw.polyNrm[i * 3 + 0];
        poly[i].normal.y = raw.polyNrm[i * 3 + 1];
        poly[i].normal.z = raw.polyNrm[i * 3 + 2];
        // SoH plane: normal.p + dist == 0; OoT3D plane: n.p == -dist -> SoH dist == OoT3D dist.
        poly[i].dist = (s16)lrintf(raw.polyDist[i]);
        // Floor polys: re-source cam+exit (low 13 bits) from the N64 floor at this triangle's
        // centroid so exits/cameras align with the N64 spawn points (see header above). When no
        // N64 floor exists under it (OoT3D-only geometry), drop the exit bits so a stray OoT3D
        // exit triangle can't bounce Link; keep the OoT3D cam region as a best guess.
        ny = COLPOLY_GET_NORMAL(poly[i].normal.y);
        if (ny > 0.5f) {
            Vec3s* va = &vtx[poly[i].flags_vIA];
            Vec3s* vb = &vtx[poly[i].flags_vIB];
            Vec3s* vc = &vtx[poly[i].vIC];
            float cx = (va->x + vb->x + vc->x) / 3.0f;
            float cy = (va->y + vb->y + vc->y) / 3.0f;
            float cz = (va->z + vb->z + vc->z) / 3.0f;
            u32 n0;
            if (Zelda3D_N64FloorData0(n64, cx, cy, cz, &n0)) {
                d0 = (d0 & ~0x1FFFu) | (n0 & 0x1FFFu);
            } else {
                d0 = d0 & ~0x1F00u; // no N64 floor here -> no exit
            }
        } else if (ny <= 0.5f && ny >= -0.5f) {
            // Wall: re-source the wall-climb property (bits 21..25) from the authoritative N64 wall
            // at this triangle. OoT3D's per-triangle wall property is unreliable (#25: it splits a
            // climbable quad into a climbable + a non-climbable triangle, dropping Link mid-climb).
            Vec3s* va = &vtx[poly[i].flags_vIA];
            Vec3s* vb = &vtx[poly[i].flags_vIB];
            Vec3s* vc = &vtx[poly[i].vIC];
            float cx = (va->x + vb->x + vc->x) / 3.0f;
            float cy = (va->y + vb->y + vc->y) / 3.0f;
            float cz = (va->z + vb->z + vc->z) / 3.0f;
            float onx = COLPOLY_GET_NORMAL(poly[i].normal.x);
            float onz = COLPOLY_GET_NORMAL(poly[i].normal.z);
            u32 n0;
            if (Zelda3D_N64WallData0(n64, cx, cy, cz, onx, onz, &n0)) {
                d0 = (d0 & ~0x03E00000u) | (n0 & 0x03E00000u);
            }
        }
        sSurfaceTypes[i].data[0] = d0;
        sSurfaceTypes[i].data[1] = d1;
    }

    // #74 — heal tessellated climbable walls: flow the climb bit from a climbable wall triangle to
    // its edge-sharing co-planar siblings, so an OoT3D quad split into climbable + non-climbable
    // triangles becomes uniformly climbable and Link doesn't dismount at the diagonal seam.
    Zelda3D_PropagateWallClimbBits(poly, raw.numPolys, sSurfaceTypes);

    // #5 — append the generated stair treads as horizontal floor polys. They sit on/above the
    // OoT3D ramp (left intact below), so BgCheck returns the tread as Link's floor and he stands
    // on the visible steps. Each tread inherits the surface type (floor material + N64-sourced
    // exit/cam) of the kaidan ramp poly directly beneath it.
    for (i = 0; i < stairNV; i++) {
        s16 x = (s16)lrintf(stairV[i * 3 + 0]);
        s16 y = (s16)lrintf(stairV[i * 3 + 1]);
        s16 z = (s16)lrintf(stairV[i * 3 + 2]);
        vtx[raw.numVerts + i].x = x;
        vtx[raw.numVerts + i].y = y;
        vtx[raw.numVerts + i].z = z;
        if (x < minX) minX = x; if (x > maxX) maxX = x;
        if (y < minY) minY = y; if (y > maxY) maxY = y;
        if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
    }
    for (i = 0; i < stairNT; i++) {
        int pi = raw.numPolys + i;
        int ia = raw.numVerts + stairT[i * 3 + 0];
        int ib = raw.numVerts + stairT[i * 3 + 1];
        int ic = raw.numVerts + stairT[i * 3 + 2];
        Vec3s* va = &vtx[ia];
        Vec3s* vb = &vtx[ib];
        Vec3s* vc = &vtx[ic];
        float cx = (va->x + vb->x + vc->x) / 3.0f;
        float cy = (va->y + vb->y + vc->y) / 3.0f;
        float cz = (va->z + vb->z + vc->z) / 3.0f;
        u32 n0;
        poly[pi].type = pi;
        poly[pi].flags_vIA = ia & 0x1FFF;
        poly[pi].flags_vIB = ib & 0x1FFF;
        poly[pi].vIC = ic & 0x1FFF;
        poly[pi].normal.x = 0;
        poly[pi].normal.y = COLPOLY_SNORMAL(1.0f); // horizontal tread, +Y up
        poly[pi].normal.z = 0;
        poly[pi].dist = (s16)lrintf(-cy); // plane n.p + dist == 0, n=(0,1,0) -> dist = -y
        // Surface type: take the kaidan ramp's FLOOR material (data1 + the non-exit bits of data0),
        // but re-source cam+exit (low 13 bits) from the N64 floor at the tread centroid — EXACTLY
        // as the main floor loop does. Copying the ramp's data0 wholesale is unsafe: the nearest
        // base poly under a tread can be an adjacent EXIT triangle (the entrance staircase abuts the
        // Hyrule Field transition), which would warp Link the instant he stepped on that tread.
        {
            int b = Zelda3D_BaseFloorPoly(vtx, poly, raw.numPolys, cx, cy, cz);
            u32 d0 = (b >= 0) ? sSurfaceTypes[b].data[0] : 0;
            u32 d1 = (b >= 0) ? sSurfaceTypes[b].data[1] : 0;
            if (Zelda3D_N64FloorData0(n64, cx, cy, cz, &n0)) {
                d0 = (d0 & ~0x1FFFu) | (n0 & 0x1FFFu);
            } else {
                d0 = d0 & ~0x1F00u; // no N64 floor here -> no exit
            }
            sSurfaceTypes[pi].data[0] = d0;
            sSurfaceTypes[pi].data[1] = d1;
        }
    }
    if (stairNT > 0) {
        fprintf(stderr, "[Zelda3D] stairs: spliced %d stepped tread polys (%d verts) into %s collision\n",
               stairNT, stairNV, sceneName);
    }
    Zelda3D_FreeStairTreads(stairV, stairT);

    h->minBounds.x = minX; h->minBounds.y = minY; h->minBounds.z = minZ;
    h->maxBounds.x = maxX; h->maxBounds.y = maxY; h->maxBounds.z = maxZ;
    h->numVertices = (u16)(raw.numVerts + stairNV);
    h->vtxList = vtx;
    h->numPolygons = (u16)(raw.numPolys + stairNT);
    h->polyList = poly;
    h->surfaceTypeList = sSurfaceTypes;
    h->cameraDataList = sCamData;
    h->cameraDataListLen = camLen;
    h->numWaterBoxes = (sWaterBoxes != NULL && n64 != NULL) ? n64->numWaterBoxes : 0;
    h->waterBoxes = sWaterBoxes;

    Zelda3D_FreeRawCollision(&raw);
    sHeader = h;
    return h;
}
