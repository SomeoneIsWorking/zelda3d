// Zelda3D procedural stair geometry — replace each flat "kaidan" ramp group with real stepped 3D
// geometry (treads + risers) on the same material. Split out of zelda3d_model.cpp (pure move). The
// thin per-model driver generateRoomStairs() and the live-toggle setters stay in zelda3d_model.cpp
// (they touch the model cache); these are the geometry generators it calls. See #5.
#ifndef ZELDA3D_STAIRS_H
#define ZELDA3D_STAIRS_H

#include "asset/cmb.h"
#include <vector>
#include <cstdint>

extern float gZelda3dStairRiserY; // generated step rise (world-units/step), runtime tunable
extern int gZelda3dStairs;        // env ZELDA3D_STAIRS / REPL `stairs` gate

const std::vector<uint8_t>& stairStoneTex(int& w, int& h);   // cached custom stair texture
bool texNameIsKaidan(const Zelda3D::Cmb& cmb, int matIndex); // is this material a kaidan ramp?
void generateStairsGroup(Zelda3D::CmbDrawGroup& g);          // ramp group -> stepped geometry
void ensureStairsEnv(void);                                  // apply env overrides once

#include <array>

// Shared kaidan-patch analysis — also used by the collision-side stair-tread collector in
// zelda3d_model.cpp (Zelda3D_CollectSceneStairTreads), so it produces collision matching the render geometry.
struct StairFrame {
    float aDir[3], cDir[3];
    float amin, amax, cmin, cmax, ymin, ymax;
    int N;
    float da, dy;
};
std::vector<std::array<float, 3>> stairTriNormals(const Zelda3D::CmbDrawGroup& g);
std::vector<std::vector<int>> stairPatches(const Zelda3D::CmbDrawGroup& g,
                                           const std::vector<std::array<float, 3>>& triNrm);
bool stairFrameOf(const Zelda3D::CmbDrawGroup& g, const std::vector<int>& tris,
                  const std::vector<std::array<float, 3>>& triNrm, StairFrame& out);

#endif // ZELDA3D_STAIRS_H
