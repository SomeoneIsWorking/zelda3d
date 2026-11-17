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
void ensureStairsEnv(void);                                  // apply env overrides once

#include <array>

// Shared kaidan-patch analysis — also used by the collision-side stair-tread collector in
// zelda3d_model.cpp (Zelda3D_CollectSceneStairTreads), so it produces collision matching the render geometry.
// The u range of a kaidan texture that paints steps; outside it, the curbs painted along the sides.
struct StairCurbs {
    float first = 0.0f;
    float last = 1.0f;
};
StairCurbs stairTextureCurbs(const Zelda3D::Cmb& cmb, int matIndex);

struct StairSpan {
    float c0, c1;
    bool curb;
};

struct StairFrame {
    float aDir[3], cDir[3];
    float amin, amax, cmin, cmax, ymin, ymax;
    int N;
    float da, dy;
    // Across the flight from cmin to cmax: stepped runs and the painted curbs between them.
    std::vector<StairSpan> spans;
};
void generateStairsGroup(Zelda3D::CmbDrawGroup& g, const StairCurbs& curbs); // ramp group -> stepped geometry
// The ramp's height at a, and a curb's top there: on the step noses, level with the landing past them.
inline float stairRampY(const StairFrame& f, float a) {
    return f.ymin + (a - f.amin) * f.dy / f.da;
}
inline float stairCurbTop(const StairFrame& f, float a) {
    const float y = stairRampY(f, a) + f.dy;
    return y < f.ymax ? y : f.ymax;
}
std::vector<std::array<float, 3>> stairTriNormals(const Zelda3D::CmbDrawGroup& g);
std::vector<std::vector<int>> stairPatches(const Zelda3D::CmbDrawGroup& g,
                                           const std::vector<std::array<float, 3>>& triNrm);
bool stairFrameOf(const Zelda3D::CmbDrawGroup& g, const std::vector<int>& tris,
                  const std::vector<std::array<float, 3>>& triNrm, const StairCurbs& curbs, StairFrame& out);

#endif // ZELDA3D_STAIRS_H
