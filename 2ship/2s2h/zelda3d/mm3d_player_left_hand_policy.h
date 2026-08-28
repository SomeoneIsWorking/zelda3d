#pragma once

#include <cstdint>
#include <optional>

#include "mm3d_player_model_policy.h"
#include "mm3d_player_sheath_policy.h"

namespace Zelda3D::MM3D {

enum class PlayerLeftHandType {
    Open,
    Closed,
    OneHandSword,
    TwoHandSword,
    Four,
    Bottle,
};

enum class PlayerLeftHandModel {
    Open,
    Closed,
    OneHandSword,
    TwoHandSword,
    Bottle,
};

enum class PlayerLeftHandAnimationOverride {
    Closed,
    Open,
};

enum class PlayerBottleContentAnimation {
    Other,
    BugIn,
    BugMiss,
    BugOut,
    DrinkDemoEnd,
    DrinkDemoStart,
    DrinkDemoWait,
    FishIn,
    FishMiss,
    FishOut,
    DekuDrink,
    DekuDrinkEnd,
    DekuDrinkStart,
};

struct PlayerLeftHandState {
    PlayerModelForm form;
    PlayerLeftHandType type;
    PlayerLeftHandModel defaultModel;
    PlayerSword sword;
    std::optional<PlayerLeftHandAnimationOverride> animationOverride;
    float speed;
    float animationFrame;
    float itemChangeFrame;
    float itemChangeEndFrame;
    int currentBoots;
    int itemAction;
    int heldItemAction;
    bool state2;
    bool state400;
    bool swimming;
    bool carryingActor;
    bool zoraBoomerangThrown;
    bool modelForcesOpenHand;
    bool giantMask;
    bool carryUpperAction;
    bool bremenMarchAction;
    bool zoraGuitarStart;
    bool zoraGuitarSpecial;
    bool bottleItemChangeAnimation;
    bool itemActionIsButtonEquipped;
    bool exchangeItemAction;
    bool dekuStickVisible;
    PlayerBottleContentAnimation bottleContentAnimation;
};

// Mesh groups enabled by retail FUN_00211aa4. The result is additive to the
// base, sheath, and right-hand masks and may contain the hand plus bottle
// contents and the separately-authored Deku-stick group.
std::uint64_t PlayerLeftHandMeshMaskForState(const PlayerLeftHandState& state);

// Retail computes the bottle material colour from the same route and item
// fallback used by the visibility selector. No value means the non-bottle
// branch of FUN_00211aa4 is active.
std::optional<int> PlayerBottleContentIndexForState(const PlayerLeftHandState& state);

} // namespace Zelda3D::MM3D
