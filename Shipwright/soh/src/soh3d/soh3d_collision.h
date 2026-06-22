// C-ABI bridge for OoT3D scene collision: the C++ asset side (soh3d_model.cpp, using the
// CtrRom/zcol parsers) hands raw arrays across to the C engine side (soh3d.c), which converts
// them into a SoH CollisionHeader and installs it into play->colCtx. Dep-free (stdint only) so
// both the C++ and C translation units can include it.
#ifndef SOH3D_COLLISION_H
#define SOH3D_COLLISION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Raw OoT3D collision, malloc'd by SoH3D_LoadSceneCollisionRaw. Vertices are N64-unit
// world-space (same frame as the OoT3D render mesh); normals are unit*32767 (matches SoH
// COLPOLY_SNORMAL); the plane is n.p == -dist.
typedef struct {
    int16_t* verts;     // 3*numVerts: x,y,z
    int numVerts;
    uint16_t* polyVtx;  // 3*numPolys: vA,vB,vC (already & 0x1FFF)
    int16_t* polyNrm;   // 3*numPolys: nx,ny,nz
    float* polyDist;    // numPolys
    uint16_t* polyType; // numPolys: index into the surfaceType list
    int numPolys;
    uint32_t* surf0;    // numSurf: SurfaceType data[0] (N64 layout: cam/exit/flags)
    uint32_t* surf1;    // numSurf: SurfaceType data[1] (floor/material props)
    int numSurf;
} SoH3D_RawCollision;

// Load + parse <scene>_info.zsi collision for sceneName (the OoT3D folder name, e.g.
// "spot04"). Fills *out (caller owns; free with SoH3D_FreeRawCollision). Returns 1 on
// success, 0 if no ROM / no collision / parse error.
int SoH3D_LoadSceneCollisionRaw(const char* sceneName, SoH3D_RawCollision* out);
void SoH3D_FreeRawCollision(SoH3D_RawCollision* out);

// #5 — collision-side stepped stairs. Mirrors the render-side kaidan->treads transform so Link
// stands on the visible steps. Fills malloc'd world-space tread quads (treads only; the smooth
// OoT3D ramp underneath fills the gaps). 3 floats per vert, 3 vert-indices per tri. Returns 1 on
// success (0 verts/tris when no kaidan stairs or stairs disabled). Free with SoH3D_FreeStairTreads.
int SoH3D_CollectSceneStairTreads(const char* sceneName, float** outVerts, int* outNVerts,
                                  int** outTris, int* outNTris);
void SoH3D_FreeStairTreads(float* verts, int* tris);

#ifdef __cplusplus
}
#endif

#endif // SOH3D_COLLISION_H
