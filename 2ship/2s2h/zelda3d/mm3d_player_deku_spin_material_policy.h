#pragma once

#include <array>
#include <optional>

#include "mm3d_player_model_policy.h"

namespace Zelda3D::MM3D {

struct PlayerDekuSpinMaterialState {
    PlayerModelForm form;
    bool spinning;
    float phase;
};

struct PlayerDekuSpinMaterialOverride {
    int materialIndex;
    int constantIndex;
    std::array<float, 4> rgba;
};

// Retail Player_Draw's Deku-only alpha write at 0x001f9c9c..0x001f9d18.
// Non-Deku forms do not own this material and return no override.
std::optional<PlayerDekuSpinMaterialOverride>
PlayerDekuSpinMaterialOverrideForState(const PlayerDekuSpinMaterialState& state);

} // namespace Zelda3D::MM3D
