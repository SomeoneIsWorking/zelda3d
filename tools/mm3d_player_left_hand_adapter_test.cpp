#include "2s2h/zelda3d/mm3d_player_left_hand.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>

#include "assets/objects/gameplay_keep/gameplay_keep.h"

SaveContext gSaveContext{};

extern "C" {
Gfx* gPlayerLeftHandOpenDLs[2 * PLAYER_FORM_MAX]{};
Gfx* gPlayerLeftHandClosedDLs[2 * PLAYER_FORM_MAX]{};
Gfx* gPlayerLeftHandOneHandSwordDLs[2 * PLAYER_FORM_MAX]{};
Gfx* gPlayerLeftHandTwoHandSwordDLs[2 * PLAYER_FORM_MAX]{};
Gfx* gPlayerLeftHandBottleDLs[2 * PLAYER_FORM_MAX]{};

void Player_Action_11(Player*, PlayState*) {}

void Player_Action_ExchangeItem(Player*, PlayState*) {}

s32 Player_UpperAction_CarryActor(Player*, PlayState*) {
    return 1;
}

PlayerItemAction Player_ItemToItemAction(Player*, ItemId item) {
    switch (item) {
        case ITEM_BOTTLE:
            return PLAYER_IA_BOTTLE_EMPTY;
        case ITEM_FISH:
            return PLAYER_IA_BOTTLE_FISH;
        default:
            return PLAYER_IA_NONE;
    }
}
}

namespace {

std::uint64_t Mask(std::initializer_list<int> meshIds) {
    std::uint64_t mask = 0;
    for (const int meshId : meshIds) {
        mask |= std::uint64_t{ 1 } << meshId;
    }
    return mask;
}

bool Near(float actual, int component) {
    return std::fabs(actual - static_cast<float>(component) / 255.0F) < 1e-7F;
}

Player MakePlayer(PlayerAnimationFrame& frame) {
    Player player{};
    player.transformation = PLAYER_FORM_HUMAN;
    player.leftHandType = PLAYER_MODELTYPE_LH_ONE_HAND_SWORD;
    player.currentBoots = PLAYER_BOOTS_HYLIAN;
    player.skelAnime.jointTable = frame.frameTable;
    player.leftHandDLists = &gPlayerLeftHandOneHandSwordDLs[2 * PLAYER_FORM_HUMAN];
    return player;
}

} // namespace

int main() {
    PlayerAnimationFrame frame{};
    Player player = MakePlayer(frame);
    unsigned long long mask = 0;
    Zelda3DMMPlayerBottleMaterialOverride bottleMaterial{};

    assert(Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_RAZOR, &mask));
    assert(mask == Mask({ 14 }));
    assert(Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_GILDED, &mask));
    assert(mask == Mask({ 16 }));

    player.leftHandType = PLAYER_MODELTYPE_LH_OPEN;
    player.leftHandDLists = &gPlayerLeftHandOpenDLs[2 * PLAYER_FORM_HUMAN];
    player.actor.speed = 3.0F;
    assert(Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_NONE, &mask));
    assert(mask == Mask({ 20 }));
    player.actionFunc = Player_Action_11;
    assert(Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_NONE, &mask));
    assert(mask == Mask({ 21 }));

    player.actionFunc = nullptr;
    player.actor.speed = 0.0F;
    frame.appearanceInfo = 0x1000;
    assert(Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_NONE, &mask));
    assert(mask == Mask({ 20 }));
    frame.appearanceInfo = 0x2000;
    assert(Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_NONE, &mask));
    assert(mask == Mask({ 21 }));
    frame.appearanceInfo = 0x3000;
    assert(!Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_NONE, &mask));

    frame.appearanceInfo = 0;
    player.transformation = PLAYER_FORM_ZORA;
    player.leftHandType = PLAYER_MODELTYPE_LH_CLOSED;
    player.leftHandDLists = &gPlayerLeftHandClosedDLs[2 * PLAYER_FORM_ZORA];
    Actor boomerang{};
    boomerang.id = ACTOR_EN_BOOM;
    player.zoraBoomerangActor = &boomerang;
    assert(Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_NONE, &mask));
    assert(mask == Mask({ 1 }));
    boomerang.id = ACTOR_EN_ARROW;
    assert(Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_NONE, &mask));
    assert(mask == Mask({ 2 }));
    player.zoraBoomerangActor = nullptr;

    player.transformation = PLAYER_FORM_HUMAN;
    player.leftHandType = PLAYER_MODELTYPE_LH_BOTTLE;
    player.leftHandDLists = &gPlayerLeftHandBottleDLs[2 * PLAYER_FORM_HUMAN];
    player.itemAction = PLAYER_IA_BOTTLE_FISH;
    gSaveContext.save.saveInfo.equips.buttonItems[PLAYER_FORM_FIERCE_DEITY][1] = ITEM_FISH;
    assert(Zelda3D_MM_PlayerLeftHandDrawState(&player, EQUIP_VALUE_SWORD_NONE, &mask, &bottleMaterial));
    assert(mask == Mask({ 0, 24 }));
    assert(bottleMaterial.enabled);
    assert(bottleMaterial.materialIndex == 5);
    assert(bottleMaterial.constantIndex == 0);
    assert(Near(bottleMaterial.rgba[0], 0));
    assert(Near(bottleMaterial.rgba[1], 127));
    assert(Near(bottleMaterial.rgba[2], 255));
    assert(Near(bottleMaterial.rgba[3], 255));
    gSaveContext.buttonStatus[1] = BTN_DISABLED;
    assert(Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_NONE, &mask));
    assert(mask == Mask({ 0 }));
    gSaveContext.hudVisibility = HUD_VISIBILITY_A_B_C;
    assert(Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_NONE, &mask));
    assert(mask == Mask({ 0, 24 }));
    gSaveContext.buttonStatus[1] = BTN_ENABLED;
    gSaveContext.hudVisibility = HUD_VISIBILITY_IDLE;

    player.skelAnime.animation = const_cast<char*>(gPlayerAnim_link_bottle_bug_in);
    player.skelAnime.curFrame = 11.0F;
    assert(Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_NONE, &mask));
    assert(mask == Mask({ 0 }));
    player.skelAnime.curFrame = 12.0F;
    assert(Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_NONE, &mask));
    assert(mask == Mask({ 0, 24 }));

    player.itemAction = PLAYER_IA_DEKU_STICK;
    player.skelAnime.animation = nullptr;
    assert(Zelda3D_MM_PlayerLeftHandDrawState(&player, EQUIP_VALUE_SWORD_NONE, &mask, &bottleMaterial));
    assert(mask == Mask({ 0, 27 }));
    assert(bottleMaterial.enabled);
    assert(Near(bottleMaterial.rgba[3], 0));
    player.stateFlags3 = PLAYER_STATE3_4000000;
    assert(Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_NONE, &mask));
    assert(mask == Mask({ 0 }));

    player.stateFlags3 = 0;
    player.leftHandDLists = nullptr;
    assert(!Zelda3D_MM_PlayerLeftHandMeshMask(&player, EQUIP_VALUE_SWORD_NONE, &mask));
}
