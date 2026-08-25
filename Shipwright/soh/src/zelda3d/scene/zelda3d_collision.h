// C-ABI bridge for OoT3D scene collision: the C++ asset side (zelda3d_model.cpp, using the
// CtrRom/zcol parsers) hands raw arrays across to the C engine side (zelda3d.c), which converts
// them into a SoH CollisionHeader and installs it into play->colCtx. Dep-free (stdint only) so
// both the C++ and C translation units can include it.
#ifndef ZELDA3D_COLLISION_H
#define ZELDA3D_COLLISION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Live REPL/A-B gate. The collision owner combines it with the environment default and the
// Zelda3D master enable; changes take effect on the next scene load.
extern int gZelda3dCollision;

// Raw OoT3D collision, malloc'd by Zelda3D_LoadSceneCollisionRaw. Vertices are N64-unit
// world-space (same frame as the OoT3D render mesh); normals are unit*32767 (matches SoH
// COLPOLY_SNORMAL); the plane is n.p == -dist.
typedef struct {
    int16_t* verts; // 3*numVerts: x,y,z
    int numVerts;
    uint16_t* polyVtx;  // 3*numPolys: vA,vB,vC (already & 0x1FFF)
    int16_t* polyNrm;   // 3*numPolys: nx,ny,nz
    float* polyDist;    // numPolys
    uint16_t* polyType; // numPolys: index into the surfaceType list
    int numPolys;
    uint32_t* surf0; // numSurf: SurfaceType data[0] (N64 layout: cam/exit/flags)
    uint32_t* surf1; // numSurf: SurfaceType data[1] (floor/material props)
    int numSurf;
} Zelda3D_RawCollision;

// Load + parse <scene>_info.zsi collision for sceneName (the OoT3D folder name, e.g.
// "spot04"). Fills *out (caller owns; free with Zelda3D_FreeRawCollision). Returns 1 on
// success, 0 if no ROM / no collision / parse error.
int Zelda3D_LoadSceneCollisionRaw(const char* sceneName, Zelda3D_RawCollision* out);
void Zelda3D_FreeRawCollision(Zelda3D_RawCollision* out);

// #5 — collision-side stepped stairs. Mirrors the render-side kaidan->treads transform so Link
// stands on the visible steps. Fills malloc'd world-space tread quads (treads only; the smooth
// OoT3D ramp underneath fills the gaps). 3 floats per vert, 3 vert-indices per tri. Returns 1 on
// success (0 verts/tris when no kaidan stairs or stairs disabled). Free with Zelda3D_FreeStairTreads.
int Zelda3D_CollectSceneStairTreads(const char* sceneName, float** outVerts, int* outNVerts, int** outTris,
                                    int* outNTris);
void Zelda3D_FreeStairTreads(float* verts, int* tris);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_COLLISION_H
