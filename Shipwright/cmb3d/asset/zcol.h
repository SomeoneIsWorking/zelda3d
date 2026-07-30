// Parser for OoT3D **and MM3D** scene collision (the N64 cmd-0x03 analogue) inside `<scene>_info.zsi`.
//
// MM3D (ZSI\x09) differs from OoT3D (ZSI\x01) in exactly two places, both version-gated below:
//   * the COUNT triplet (nVtx/nPoly/nSurf) sits at +0x1e/+0x20/+0x22 instead of +0x1c/+0x1e/+0x20
//     (the bbox preceding it is at +0x12, not +0x10). The POINTERS at +0x28/+0x2c/+0x30 do NOT move.
//   * CollisionPoly omits OoT3D's 2-byte flags word at +0x06, so the normal starts at +0x06, not
//     +0x08. Stride is still 20, the array is still anchored at polyList-2, dist is still f32 @+0x0e.
// Derived with tools/zelda3d_collision_layout.cpp and guarded by tools/zelda3d_collision_test.cpp:
// 110/111 MM3D scenes parse with plane identity 100% and face-normal agreement 99.9%.
// Port of tools/oot3d_collision.py. Pure C++ (no SoH/LUS deps).
//
// noclip has the `Collision = 0x03` enum but never parses it — this layout was
// reverse-engineered from the USA decrypted ROM (verified across 6 scenes: the plane
// identity n.vA == -dist holds ~100%, stored normal == geometric face normal 99.9%).
// See tools/oot3d_collision.py / PROGRESS.md for the full derivation.
//
// CollisionHeader (at the cmd-0x03 file offset; command addresses are PLAIN file offsets):
//   +0x1c u16 nVtx, +0x1e u16 nPoly, +0x28 u32 vtxList, +0x2c u32 polyList,
//   +0x30 surfaceTypeList, +0x34 camData, +0x38 waterBox.
//   Internal data pointers are NOT plain offsets: vertex data = vtxList + 0x10; the polygon array
//   is anchored at polyList + 18 (NOT -2; see zcol.cpp — brute-forced over all 114 scenes, 100.000%
//   valid and exactly bounded, where -2 gives 99.933% plus a phantom record 0).
// Vertex: Vec3s (s16 x,y,z), stride 6, from vtxList+0x10.
// CollisionPoly: 20 bytes from polyList+18: +0 vA, +2 vB, +4 vC (u16, &0x1FFF; top 3 = flags),
//   +6 u16 flags, +8 s16 nx/+0xa ny/+0xc nz (normal /32767), +0xe f32 dist (n.p == -dist),
//   +0x12 u16 pad.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Zelda3D {

class OoT3DCollision {
  public:
    struct Vert {
        int16_t x, y, z;
    };
    struct Poly {
        uint16_t vA, vB, vC; // already masked to 0x1FFF
        int16_t nx, ny, nz;  // unit normal * 32767 (raw s16, matches SoH COLPOLY_SNORMAL)
        float dist;          // plane: n.p == -dist
        uint16_t type;       // index into surfaces() (the +0x12 record field)
    };
    // SurfaceType: same bitfield layout as N64 OoT (same game). data0 low byte = bgCamIndex,
    // (data0>>8)&0x1F = sceneExitIndex, plus floor/wall flags; data1 = floor/material props.
    struct Surface {
        uint32_t data0, data1;
    };

    explicit OoT3DCollision(const std::vector<uint8_t>& data);
    bool ok() const { return mOk; }
    const std::string& error() const { return mErr; }

    const std::vector<Vert>& verts() const { return mVerts; }
    const std::vector<Poly>& polys() const { return mPolys; }
    const std::vector<Surface>& surfaces() const { return mSurfaces; }

  private:
    bool mOk = false;
    std::string mErr;
    std::vector<Vert> mVerts;
    std::vector<Poly> mPolys;
    std::vector<Surface> mSurfaces;
};

} // namespace Zelda3D
