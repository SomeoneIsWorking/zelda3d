#pragma once

#include <unordered_map>
#include <vector>

#include <libultraship/libultra/gbi.h>

void Zelda3D_RunGraphicsCommands(Gfx* commands, const std::vector<std::unordered_map<Mtx*, MtxF>>& replacements,
                                 const std::vector<float>& interpolationSteps);
