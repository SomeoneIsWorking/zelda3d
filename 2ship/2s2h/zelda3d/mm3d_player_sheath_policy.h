#pragma once

#include <cstdint>

#include "mm3d_player_model_policy.h"

namespace Zelda3D::MM3D {

enum class PlayerSheathType {
    Type12,
    Type13,
    Type14,
    Type15,
};

enum class PlayerShield {
    None,
    Hero,
    Mirror,
};

enum class PlayerSword {
    None,
    Kokiri,
    Razor,
    Gilded,
};

bool PlayerSwordFromRetailIndex(int value, PlayerSword& result);

struct PlayerSheathState {
    PlayerModelForm form;
    PlayerSheathType sheathType;
    PlayerShield shield;
    PlayerSword sword;
    bool giantMask;
};

// Equipment groups enabled by MM3D Player_Draw's complete sheath-limb stage.
// The returned additions are ORed with the form's base visibility mask.
std::uint64_t PlayerSheathMeshMaskForState(const PlayerSheathState& state);

} // namespace Zelda3D::MM3D
