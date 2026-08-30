#include "mm3d_player_left_hand.h"

#include <cstring>
#include <optional>

#include "assets/objects/gameplay_keep/gameplay_keep.h"
#include "mm3d_player_bottle_material_policy.h"
#include "mm3d_player_left_hand_policy.h"
#include "mm3d_player_model_policy.h"

extern "C" {
void Player_Action_11(Player* player, PlayState* play);
void Player_Action_ExchangeItem(Player* player, PlayState* play);
s32 Player_UpperAction_CarryActor(Player* player, PlayState* play);
PlayerItemAction Player_ItemToItemAction(Player* player, ItemId item);

extern Gfx* gPlayerLeftHandOpenDLs[];
extern Gfx* gPlayerLeftHandClosedDLs[];
extern Gfx* gPlayerLeftHandOneHandSwordDLs[];
extern Gfx* gPlayerLeftHandTwoHandSwordDLs[];
extern Gfx* gPlayerLeftHandBottleDLs[];
}

namespace Zelda3D::MM3D {
namespace {

bool AnimationEqual(const void* animation, const char* resourceName) {
    return animation != nullptr && resourceName != nullptr &&
           std::strcmp(static_cast<const char*>(animation), resourceName) == 0;
}

bool ToLeftHandType(int value, PlayerLeftHandType& result) {
    switch (value) {
        case PLAYER_MODELTYPE_LH_OPEN:
            result = PlayerLeftHandType::Open;
            return true;
        case PLAYER_MODELTYPE_LH_CLOSED:
            result = PlayerLeftHandType::Closed;
            return true;
        case PLAYER_MODELTYPE_LH_ONE_HAND_SWORD:
            result = PlayerLeftHandType::OneHandSword;
            return true;
        case PLAYER_MODELTYPE_LH_TWO_HAND_SWORD:
            result = PlayerLeftHandType::TwoHandSword;
            return true;
        case PLAYER_MODELTYPE_LH_4:
            result = PlayerLeftHandType::Four;
            return true;
        case PLAYER_MODELTYPE_LH_BOTTLE:
            result = PlayerLeftHandType::Bottle;
            return true;
        default:
            return false;
    }
}

bool DefaultModelForPlayer(const Player& player, PlayerLeftHandModel& result) {
    const std::size_t formOffset = static_cast<std::size_t>(player.transformation) * 2;
    if (player.leftHandDLists == &gPlayerLeftHandOpenDLs[formOffset]) {
        result = PlayerLeftHandModel::Open;
    } else if (player.leftHandDLists == &gPlayerLeftHandClosedDLs[formOffset]) {
        result = PlayerLeftHandModel::Closed;
    } else if (player.leftHandDLists == &gPlayerLeftHandOneHandSwordDLs[formOffset]) {
        result = PlayerLeftHandModel::OneHandSword;
    } else if (player.leftHandDLists == &gPlayerLeftHandTwoHandSwordDLs[formOffset]) {
        result = PlayerLeftHandModel::TwoHandSword;
    } else if (player.leftHandDLists == &gPlayerLeftHandBottleDLs[formOffset]) {
        result = PlayerLeftHandModel::Bottle;
    } else {
        return false;
    }
    return true;
}

bool AnimationOverrideForPlayer(const Player& player, std::optional<PlayerLeftHandAnimationOverride>& result) {
    if (player.skelAnime.jointTable == nullptr) {
        return false;
    }
    const int encoded = GET_LEFT_HAND_INDEX_FROM_JOINT_TABLE(player.skelAnime.jointTable);
    if (encoded == 0) {
        result.reset();
        return true;
    }
    const int index = (encoded >> 12) - 1;
    if (index == 0) {
        result = PlayerLeftHandAnimationOverride::Closed;
        return true;
    }
    if (index == 1) {
        result = PlayerLeftHandAnimationOverride::Open;
        return true;
    }
    return false;
}

bool CurrentItemActionIsButtonEquipped(Player& player) {
    for (int button = 0; button < 4; ++button) {
        const int form = button == EQUIP_SLOT_B && player.transformation != PLAYER_FORM_HUMAN
                             ? player.transformation
                             : PLAYER_FORM_FIERCE_DEITY;
        const auto item = static_cast<ItemId>(gSaveContext.buttonStatus[button] == BTN_DISABLED &&
                                                      gSaveContext.hudVisibility != HUD_VISIBILITY_A_B_C
                                                  ? ITEM_NONE
                                                  : gSaveContext.save.saveInfo.equips.buttonItems[form][button]);
        if (Player_ItemToItemAction(&player, item) == player.itemAction) {
            return true;
        }
    }
    return false;
}

PlayerBottleContentAnimation BottleContentAnimationForPlayer(const Player& player) {
    const void* animation = player.skelAnime.animation;
    if (AnimationEqual(animation, gPlayerAnim_link_bottle_bug_in)) {
        return PlayerBottleContentAnimation::BugIn;
    }
    if (AnimationEqual(animation, gPlayerAnim_link_bottle_bug_miss)) {
        return PlayerBottleContentAnimation::BugMiss;
    }
    if (AnimationEqual(animation, gPlayerAnim_link_bottle_bug_out)) {
        return PlayerBottleContentAnimation::BugOut;
    }
    if (AnimationEqual(animation, gPlayerAnim_link_bottle_drink_demo_end)) {
        return PlayerBottleContentAnimation::DrinkDemoEnd;
    }
    if (AnimationEqual(animation, gPlayerAnim_link_bottle_drink_demo_start)) {
        return PlayerBottleContentAnimation::DrinkDemoStart;
    }
    if (AnimationEqual(animation, gPlayerAnim_link_bottle_drink_demo_wait)) {
        return PlayerBottleContentAnimation::DrinkDemoWait;
    }
    if (AnimationEqual(animation, gPlayerAnim_link_bottle_fish_in)) {
        return PlayerBottleContentAnimation::FishIn;
    }
    if (AnimationEqual(animation, gPlayerAnim_link_bottle_fish_miss)) {
        return PlayerBottleContentAnimation::FishMiss;
    }
    if (AnimationEqual(animation, gPlayerAnim_link_bottle_fish_out)) {
        return PlayerBottleContentAnimation::FishOut;
    }
    if (AnimationEqual(animation, gPlayerAnim_pn_drink)) {
        return PlayerBottleContentAnimation::DekuDrink;
    }
    if (AnimationEqual(animation, gPlayerAnim_pn_drinkend)) {
        return PlayerBottleContentAnimation::DekuDrinkEnd;
    }
    if (AnimationEqual(animation, gPlayerAnim_pn_drinkstart)) {
        return PlayerBottleContentAnimation::DekuDrinkStart;
    }
    return PlayerBottleContentAnimation::Other;
}

bool StateForPlayer(Player& player, int swordEquipValue, PlayerLeftHandState& state) {
    if (!PlayerModelFormFromRetailIndex(player.transformation, state.form) ||
        !ToLeftHandType(player.leftHandType, state.type) || !DefaultModelForPlayer(player, state.defaultModel) ||
        !PlayerSwordFromRetailIndex(swordEquipValue, state.sword) ||
        !AnimationOverrideForPlayer(player, state.animationOverride)) {
        return false;
    }

    state.speed = player.actor.speed;
    state.animationFrame = player.skelAnime.curFrame;
    state.itemChangeFrame = player.skelAnimeUpper.curFrame;
    state.itemChangeEndFrame = player.skelAnimeUpper.endFrame;
    state.currentBoots = player.currentBoots;
    state.itemAction = player.itemAction;
    state.heldItemAction = player.heldItemAction;
    state.state2 = (player.stateFlags1 & PLAYER_STATE1_2) != 0;
    state.state400 = (player.stateFlags1 & PLAYER_STATE1_400) != 0;
    state.swimming = (player.stateFlags1 & PLAYER_STATE1_8000000) != 0;
    state.carryingActor = (player.stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) != 0;
    state.zoraBoomerangThrown = (player.stateFlags1 & PLAYER_STATE1_ZORA_BOOMERANG_THROWN) != 0;
    // One exact producer of MM3D Player+0x129bc bit 16 is En_Boom: both update paths set it while
    // a Zora boomerang actor is live, and destruction clears it while setting
    // PLAYER_STATE3_ZORA_BOOMERANG_CAUGHT. The N64 engine owns that lifetime through
    // zoraBoomerangActor. Check the actor identity because Deku flight reuses that pointer for a
    // nut projectile. The separate mount-transition producer remains inactive until its typed
    // host predicate is grounded.
    state.modelForcesOpenHand = player.zoraBoomerangActor != nullptr && player.zoraBoomerangActor->id == ACTOR_EN_BOOM;
    state.giantMask = player.currentMask == PLAYER_MASK_GIANT;
    state.carryUpperAction = player.upperActionFunc == Player_UpperAction_CarryActor;
    state.bremenMarchAction = player.actionFunc == Player_Action_11;
    state.zoraGuitarStart = AnimationEqual(player.skelAnime.animation, gPlayerAnim_pz_gakkistart);
    state.zoraGuitarSpecial = AnimationEqual(player.skelAnime.animation, gPlayerAnim_pz_gakkiplay);
    state.bottleItemChangeAnimation =
        AnimationEqual(player.skelAnimeUpper.animation, gPlayerAnim_link_normal_free2freeB);
    state.itemActionIsButtonEquipped = CurrentItemActionIsButtonEquipped(player);
    state.exchangeItemAction = player.actionFunc == Player_Action_ExchangeItem;
    state.dekuStickVisible =
        player.itemAction == PLAYER_IA_DEKU_STICK && (player.stateFlags3 & PLAYER_STATE3_4000000) == 0;
    state.bottleContentAnimation = BottleContentAnimationForPlayer(player);
    return true;
}

} // namespace
} // namespace Zelda3D::MM3D

extern "C" int Zelda3D_MM_PlayerLeftHandDrawState(Player* player, int swordEquipValue, unsigned long long* meshMask,
                                                  Zelda3DMMPlayerBottleMaterialOverride* bottleMaterial) {
    using namespace Zelda3D::MM3D;
    if (player == nullptr || meshMask == nullptr || bottleMaterial == nullptr) {
        return 0;
    }

    PlayerLeftHandState state{};
    if (!StateForPlayer(*player, swordEquipValue, state)) {
        return 0;
    }
    *meshMask = PlayerLeftHandMeshMaskForState(state);

    *bottleMaterial = {};
    if (const std::optional<PlayerBottleMaterialOverride> override = PlayerBottleMaterialOverrideForState(state);
        override.has_value()) {
        bottleMaterial->enabled = 1;
        bottleMaterial->materialIndex = override->materialIndex;
        bottleMaterial->constantIndex = override->constantIndex;
        for (std::size_t component = 0; component < override->rgba.size(); ++component) {
            bottleMaterial->rgba[component] = override->rgba[component];
        }
    }
    return 1;
}

extern "C" int Zelda3D_MM_PlayerLeftHandMeshMask(Player* player, int swordEquipValue, unsigned long long* meshMask) {
    Zelda3DMMPlayerBottleMaterialOverride bottleMaterial{};
    return Zelda3D_MM_PlayerLeftHandDrawState(player, swordEquipValue, meshMask, &bottleMaterial);
}
