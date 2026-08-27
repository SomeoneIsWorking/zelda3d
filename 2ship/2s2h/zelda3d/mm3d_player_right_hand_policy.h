#pragma once

#include <cstdint>
#include <optional>

#include "mm3d_player_model_policy.h"
#include "mm3d_player_sheath_policy.h"

namespace Zelda3D::MM3D {

enum class PlayerRightHandType {
    Open,
    Closed,
    Shield,
    Bow,
    Instrument,
    Hookshot,
    Disabled,
};

enum class PlayerRightHandModel {
    Open,
    Closed,
    Bow,
    Instrument,
    Hookshot,
};

enum class PlayerRightHandAnimationOverride {
    Closed,
    Open,
};

struct PlayerRightHandState {
    PlayerModelForm form;
    PlayerRightHandType type;
    PlayerRightHandModel defaultModel;
    PlayerShield shield;
    std::optional<PlayerRightHandAnimationOverride> animationOverride;
    float speed;
    int currentBoots;
    bool state2;
    bool state400;
    bool swimming;
    bool carryingActor;
    bool giantMask;
    bool carryUpperAction;
    bool dekuDrinkAnimation;
};

// The one group enabled by MM3D's complete right-hand stage tail-shared at
// 0x00211fd4 from FUN_00201074. This is additive to the base and sheath masks.
std::uint64_t PlayerRightHandMeshMaskForState(const PlayerRightHandState& state);

} // namespace Zelda3D::MM3D
