#pragma once

#include <array>
#include <optional>

#include "mm3d_player_left_hand_policy.h"

namespace Zelda3D::MM3D {

struct PlayerBottleMaterialOverride {
    int materialIndex;
    int constantIndex;
    std::array<float, 4> rgba;
};

// Material-constant write performed by retail FUN_00211aa4. The item index and
// bottle-route decision are owned by the left-hand selector so visibility and
// colour cannot disagree.
std::optional<PlayerBottleMaterialOverride> PlayerBottleMaterialOverrideForState(const PlayerLeftHandState& state);

} // namespace Zelda3D::MM3D
