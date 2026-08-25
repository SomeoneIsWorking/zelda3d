#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include <libultraship/libultra/gbi.h>

namespace Zelda3D::DlistHarness {

struct DlistFixture {
    std::vector<Gfx> model;
    std::vector<Gfx> commands;
    Vp viewport{};
    Mtx projectionMatrix{};
    Mtx modelViewMatrix{};
    std::array<Vtx, 3> canary{};
    std::unordered_map<Mtx*, MtxF> matrixReplacements;
    std::string viewPlane = "xy";
};

} // namespace Zelda3D::DlistHarness
