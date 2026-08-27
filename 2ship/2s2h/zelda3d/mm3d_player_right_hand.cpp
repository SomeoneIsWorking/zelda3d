#include "mm3d_player_right_hand.h"

#include <cstring>
#include <optional>

#include "assets/objects/gameplay_keep/gameplay_keep.h"
#include "mm3d_player_model_policy.h"
#include "mm3d_player_right_hand_policy.h"

extern "C" {
s32 Player_UpperAction_CarryActor(Player* player, PlayState* play);
extern Gfx* gPlayerRightHandOpenDLs[];
extern Gfx* gPlayerRightHandClosedDLs[];
extern Gfx* gPlayerRightHandBowDLs[];
extern Gfx* gPlayerRightHandInstrumentDLs[];
extern Gfx* gPlayerRightHandHookshotDLs[];
}

namespace Zelda3D::MM3D {
namespace {

bool ToRightHandType(int value, PlayerRightHandType& result) {
    switch (value) {
        case PLAYER_MODELTYPE_RH_OPEN:
            result = PlayerRightHandType::Open;
            return true;
        case PLAYER_MODELTYPE_RH_CLOSED:
            result = PlayerRightHandType::Closed;
            return true;
        case PLAYER_MODELTYPE_RH_SHIELD:
            result = PlayerRightHandType::Shield;
            return true;
        case PLAYER_MODELTYPE_RH_BOW:
            result = PlayerRightHandType::Bow;
            return true;
        case PLAYER_MODELTYPE_RH_INSTRUMENT:
            result = PlayerRightHandType::Instrument;
            return true;
        case PLAYER_MODELTYPE_RH_HOOKSHOT:
            result = PlayerRightHandType::Hookshot;
            return true;
        case PLAYER_MODELTYPE_RH_FF:
            result = PlayerRightHandType::Disabled;
            return true;
        default:
            return false;
    }
}

bool ToShield(int value, PlayerShield& result) {
    switch (value) {
        case PLAYER_SHIELD_NONE:
            result = PlayerShield::None;
            return true;
        case PLAYER_SHIELD_HEROS_SHIELD:
            result = PlayerShield::Hero;
            return true;
        case PLAYER_SHIELD_MIRROR_SHIELD:
            result = PlayerShield::Mirror;
            return true;
        default:
            return false;
    }
}

bool DefaultModelForPlayer(const Player& player, PlayerRightHandModel& result) {
    const std::size_t formOffset = static_cast<std::size_t>(player.transformation) * 2;
    if (player.rightHandDLists == &gPlayerRightHandOpenDLs[formOffset]) {
        result = PlayerRightHandModel::Open;
    } else if (player.rightHandDLists == &gPlayerRightHandClosedDLs[formOffset]) {
        result = PlayerRightHandModel::Closed;
    } else if (player.rightHandDLists == &gPlayerRightHandBowDLs[formOffset]) {
        result = PlayerRightHandModel::Bow;
    } else if (player.rightHandDLists == &gPlayerRightHandInstrumentDLs[formOffset]) {
        result = PlayerRightHandModel::Instrument;
    } else if (player.rightHandDLists == &gPlayerRightHandHookshotDLs[formOffset]) {
        result = PlayerRightHandModel::Hookshot;
    } else {
        return false;
    }
    return true;
}

bool AnimationOverrideForPlayer(const Player& player, std::optional<PlayerRightHandAnimationOverride>& result) {
    if (player.skelAnime.jointTable == nullptr) {
        return false;
    }
    const int encoded = GET_RIGHT_HAND_INDEX_FROM_JOINT_TABLE(player.skelAnime.jointTable);
    if (encoded == 0) {
        result.reset();
        return true;
    }
    const int index = (encoded >> 8) - 1;
    if (index == 0) {
        result = PlayerRightHandAnimationOverride::Closed;
        return true;
    }
    if (index == 1) {
        result = PlayerRightHandAnimationOverride::Open;
        return true;
    }
    return false;
}

bool AnimationEqual(const void* animation, const char* resourceName) {
    return animation != nullptr && resourceName != nullptr &&
           std::strcmp(static_cast<const char*>(animation), resourceName) == 0;
}

} // namespace
} // namespace Zelda3D::MM3D

extern "C" int Zelda3D_MM_PlayerRightHandMeshMask(const Player* player, unsigned long long* meshMask) {
    using namespace Zelda3D::MM3D;
    if (player == nullptr || meshMask == nullptr) {
        return 0;
    }

    PlayerRightHandState state{};
    if (!PlayerModelFormFromRetailIndex(player->transformation, state.form) ||
        !ToRightHandType(player->rightHandType, state.type) || !DefaultModelForPlayer(*player, state.defaultModel) ||
        !ToShield(player->currentShield, state.shield) ||
        !AnimationOverrideForPlayer(*player, state.animationOverride)) {
        return 0;
    }

    state.speed = player->actor.speed;
    state.currentBoots = player->currentBoots;
    state.state2 = (player->stateFlags1 & PLAYER_STATE1_2) != 0;
    state.state400 = (player->stateFlags1 & PLAYER_STATE1_400) != 0;
    state.swimming = (player->stateFlags1 & PLAYER_STATE1_8000000) != 0;
    state.carryingActor = (player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) != 0;
    state.giantMask = player->currentMask == PLAYER_MASK_GIANT;
    state.carryUpperAction = player->upperActionFunc == Player_UpperAction_CarryActor;
    state.dekuDrinkAnimation = AnimationEqual(player->skelAnime.animation, gPlayerAnim_pn_drink) ||
                               AnimationEqual(player->skelAnime.animation, gPlayerAnim_pn_drinkend);
    *meshMask = PlayerRightHandMeshMaskForState(state);
    return 1;
}
