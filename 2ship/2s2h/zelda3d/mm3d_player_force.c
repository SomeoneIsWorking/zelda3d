#include "2s2h/zelda3d/mm3d_player_force.h"
#include "overlays/actors/ovl_player_actor/z_player_overlay.h"

static ItemId Zelda3D_PlayerFormMaskItem(PlayerTransformation form) {
    switch (form) {
        case PLAYER_FORM_FIERCE_DEITY:
            return ITEM_MASK_FIERCE_DEITY;
        case PLAYER_FORM_GORON:
            return ITEM_MASK_GORON;
        case PLAYER_FORM_ZORA:
            return ITEM_MASK_ZORA;
        case PLAYER_FORM_DEKU:
            return ITEM_MASK_DEKU;
        default:
            return ITEM_NONE;
    }
}

s32 Zelda3D_PlayerForceGoronRoll(Player* player, PlayState* play) {
    if ((player == NULL) || (play == NULL) || (player->transformation != PLAYER_FORM_GORON)) {
        return 0;
    }

    // This is the Goron branch of the normal A-button roll handler. func_80836B3C delegates to
    // func_80836AD8, which installs Player_Action_96 and initializes the real roll controller.
    func_80836B3C(play, player, 0.0f);
    return 1;
}

Zelda3DPlayerFormRequestResult Zelda3D_PlayerRequestForm(Player* player, PlayState* play, PlayerTransformation form) {
    if ((player == NULL) || (play == NULL) || (form < PLAYER_FORM_FIERCE_DEITY) || (form >= PLAYER_FORM_MAX)) {
        return ZELDA3D_PLAYER_FORM_REQUEST_INVALID;
    }
    if (player->transformation == form) {
        return ZELDA3D_PLAYER_FORM_REQUEST_ALREADY_ACTIVE;
    }

    // Taking a transformation mask off is requested by using that same mask again. A request for a
    // non-human form instead uses the target form's mask. Player_UseItem and Player_ActionHandler_13
    // remain the sole owners of the asynchronous transition, its collision gate, and its action setup.
    PlayerTransformation maskForm = (form == PLAYER_FORM_HUMAN) ? (PlayerTransformation)player->transformation : form;
    ItemId maskItem = Zelda3D_PlayerFormMaskItem(maskForm);
    if ((maskItem == ITEM_NONE) || (INV_CONTENT(maskItem) != maskItem)) {
        return ZELDA3D_PLAYER_FORM_REQUEST_MASK_MISSING;
    }

    Player_UseItem(play, player, maskItem);
    return ZELDA3D_PLAYER_FORM_REQUEST_SENT;
}

s32 Zelda3D_PlayerRequestItem(Player* player, PlayState* play, s32 itemId) {
    if ((player == NULL) || (play == NULL) || (itemId < 0) || (itemId > UINT8_MAX)) {
        return 0;
    }

    Player_UseItem(play, player, (ItemId)itemId);
    return 1;
}

s32 Zelda3D_PlayerEquipItem(PlayState* play, EquipSlot slot, s32 itemId) {
    if ((play == NULL) || (slot < EQUIP_SLOT_C_LEFT) || (slot > EQUIP_SLOT_C_RIGHT) || (itemId < 0) ||
        (itemId > UINT8_MAX)) {
        return 0;
    }

    SET_CUR_FORM_BTN_ITEM(slot, (ItemId)itemId);
    Interface_LoadItemIcon(play, slot);
    return 1;
}
