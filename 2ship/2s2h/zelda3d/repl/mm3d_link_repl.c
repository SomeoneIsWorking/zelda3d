#include "2s2h/zelda3d/repl/mm3d_link_repl.h"

#include "2s2h/zelda3d/mm3d_link_state.h"
#include "2s2h/zelda3d/mm3d_player_force.h"

#include <stdio.h>
#include <string.h>

typedef struct Zelda3DMmLinkReplOutput {
    Zelda3DMmReplReply reply;
    void* user;
} Zelda3DMmLinkReplOutput;

static void Zelda3D_MmLinkReply(const Zelda3DMmLinkReplOutput* output, const char* line) {
    if (output->reply != NULL) {
        output->reply(line, output->user);
    }
}

static const char* Zelda3D_MmLinkFormName(PlayerTransformation form) {
    switch (form) {
        case PLAYER_FORM_FIERCE_DEITY:
            return "fd";
        case PLAYER_FORM_GORON:
            return "goron";
        case PLAYER_FORM_ZORA:
            return "zora";
        case PLAYER_FORM_DEKU:
            return "deku";
        case PLAYER_FORM_HUMAN:
            return "human";
        default:
            return "invalid";
    }
}

static const char* Zelda3D_MmLinkMaskName(PlayerMask mask) {
    switch (mask) {
        case PLAYER_MASK_NONE:
            return "none";
        case PLAYER_MASK_FIERCE_DEITY:
            return "fd";
        case PLAYER_MASK_GORON:
            return "goron";
        case PLAYER_MASK_ZORA:
            return "zora";
        case PLAYER_MASK_DEKU:
            return "deku";
        default:
            return "other";
    }
}

static s32 Zelda3D_MmLinkParseForm(const char* name, PlayerTransformation* outForm) {
    if (strcmp(name, "human") == 0) {
        *outForm = PLAYER_FORM_HUMAN;
    } else if (strcmp(name, "deku") == 0) {
        *outForm = PLAYER_FORM_DEKU;
    } else if (strcmp(name, "goron") == 0) {
        *outForm = PLAYER_FORM_GORON;
    } else if (strcmp(name, "zora") == 0) {
        *outForm = PLAYER_FORM_ZORA;
    } else if ((strcmp(name, "fd") == 0) || (strcmp(name, "fierce-deity") == 0)) {
        *outForm = PLAYER_FORM_FIERCE_DEITY;
    } else {
        return 0;
    }
    return 1;
}

static s32 Zelda3D_MmLinkParseEquipSlot(const char* name, EquipSlot* outSlot) {
    if (strcmp(name, "c-left") == 0) {
        *outSlot = EQUIP_SLOT_C_LEFT;
    } else if (strcmp(name, "c-down") == 0) {
        *outSlot = EQUIP_SLOT_C_DOWN;
    } else if (strcmp(name, "c-right") == 0) {
        *outSlot = EQUIP_SLOT_C_RIGHT;
    } else {
        return 0;
    }
    return 1;
}

static void Zelda3D_MmLinkInfo(PlayState* play, const Zelda3DMmLinkReplOutput* output) {
    char out[512];
    Player* player = (play != NULL) ? GET_PLAYER(play) : NULL;
    if (player == NULL) {
        Zelda3D_MmLinkReply(output, "linkinfo err (no player)");
        return;
    }

    Vec3f* pos = &player->actor.world.pos;
    PlayerTransformation runtimeForm = (PlayerTransformation)player->transformation;
    PlayerTransformation saveForm = (PlayerTransformation)gSaveContext.save.playerForm;
    PlayerMask currentMask = (PlayerMask)player->currentMask;
    PlayerMask equippedMask = (PlayerMask)gSaveContext.save.equippedMask;
    snprintf(out, sizeof(out),
             "linkinfo runtimeForm=%s(%d) saveForm=%s(%d) currentMask=%s(%d) equippedMask=%s(%d) "
             "ownedMask=deku:%d,goron:%d,zora:%d,fd:%d action=%s actionVar1=%d itemAction=%d "
             "itemRequest=%d heldItemId=%d heldItemAction=%d cItems=%02X,%02X,%02X "
             "stateFlags=0x%08X,0x%08X,0x%08X speedXZ=%.2f "
             "pos=(%.1f,%.1f,%.1f)",
             Zelda3D_MmLinkFormName(runtimeForm), runtimeForm, Zelda3D_MmLinkFormName(saveForm), saveForm,
             Zelda3D_MmLinkMaskName(currentMask), currentMask, Zelda3D_MmLinkMaskName(equippedMask), equippedMask,
             INV_CONTENT(ITEM_MASK_DEKU) == ITEM_MASK_DEKU, INV_CONTENT(ITEM_MASK_GORON) == ITEM_MASK_GORON,
             INV_CONTENT(ITEM_MASK_ZORA) == ITEM_MASK_ZORA,
             INV_CONTENT(ITEM_MASK_FIERCE_DEITY) == ITEM_MASK_FIERCE_DEITY, Zelda3D_PlayerActionName(player),
             player->av1.actionVar1, player->itemAction, player->unk_AA5, player->heldItemId, player->heldItemAction,
             GET_CUR_FORM_BTN_ITEM(EQUIP_SLOT_C_LEFT), GET_CUR_FORM_BTN_ITEM(EQUIP_SLOT_C_DOWN),
             GET_CUR_FORM_BTN_ITEM(EQUIP_SLOT_C_RIGHT), player->stateFlags1, player->stateFlags2, player->stateFlags3,
             player->speedXZ, pos->x, pos->y, pos->z);
    Zelda3D_MmLinkReply(output, out);
}

static const char* Zelda3D_MmLinkFormRequestStatus(Zelda3DPlayerFormRequestResult result) {
    switch (result) {
        case ZELDA3D_PLAYER_FORM_REQUEST_SENT:
            return "requested";
        case ZELDA3D_PLAYER_FORM_REQUEST_ALREADY_ACTIVE:
            return "already-active";
        case ZELDA3D_PLAYER_FORM_REQUEST_MASK_MISSING:
            return "mask-missing";
        default:
            return "invalid";
    }
}

static void Zelda3D_MmLinkForm(PlayState* play, Zelda3DMmReplArgs* args, const Zelda3DMmLinkReplOutput* output) {
    char formName[32] = { 0 };
    Player* player = (play != NULL) ? GET_PLAYER(play) : NULL;
    PlayerTransformation form;
    if (player == NULL) {
        Zelda3D_MmLinkReply(output, "linkform err (no player)");
        return;
    }
    if (!Zelda3D_MmReplNextToken(args, formName, sizeof(formName)) || !Zelda3D_MmReplArgsEnd(args) ||
        !Zelda3D_MmLinkParseForm(formName, &form)) {
        Zelda3D_MmLinkReply(output, "usage: linkform <human|deku|goron|zora|fd>");
        return;
    }

    Zelda3DPlayerFormRequestResult result = Zelda3D_PlayerRequestForm(player, play, form);
    char out[192];
    snprintf(out, sizeof(out), "linkform target=%s status=%s runtimeForm=%s saveForm=%s action=%s",
             Zelda3D_MmLinkFormName(form), Zelda3D_MmLinkFormRequestStatus(result),
             Zelda3D_MmLinkFormName((PlayerTransformation)player->transformation),
             Zelda3D_MmLinkFormName((PlayerTransformation)gSaveContext.save.playerForm),
             Zelda3D_PlayerActionName(player));
    Zelda3D_MmLinkReply(output, out);
}

static void Zelda3D_MmLinkItem(PlayState* play, Zelda3DMmReplArgs* args, const Zelda3DMmLinkReplOutput* output) {
    Player* player = (play != NULL) ? GET_PLAYER(play) : NULL;
    int32_t itemId;
    if (player == NULL) {
        Zelda3D_MmLinkReply(output, "linkitem err (no player)");
        return;
    }
    if (!Zelda3D_MmReplParseI32(args, 0, &itemId) || !Zelda3D_MmReplArgsEnd(args) ||
        !Zelda3D_PlayerRequestItem(player, play, itemId)) {
        Zelda3D_MmLinkReply(output, "usage: linkitem <ItemId 0x00..0xff>");
        return;
    }

    char out[192];
    snprintf(out, sizeof(out), "linkitem requested=0x%02X itemAction=%d itemRequest=%d heldItemId=%d heldItemAction=%d",
             (unsigned)itemId, player->itemAction, player->unk_AA5, player->heldItemId, player->heldItemAction);
    Zelda3D_MmLinkReply(output, out);
}

static void Zelda3D_MmLinkEquip(PlayState* play, Zelda3DMmReplArgs* args, const Zelda3DMmLinkReplOutput* output) {
    char slotName[16] = { 0 };
    EquipSlot slot;
    int32_t itemId;
    if (!Zelda3D_MmReplNextToken(args, slotName, sizeof(slotName)) || !Zelda3D_MmLinkParseEquipSlot(slotName, &slot) ||
        !Zelda3D_MmReplParseI32(args, 0, &itemId) || !Zelda3D_MmReplArgsEnd(args) ||
        !Zelda3D_PlayerEquipItem(play, slot, itemId)) {
        Zelda3D_MmLinkReply(output, "usage: linkequip <c-left|c-down|c-right> <ItemId 0x00..0xff>");
        return;
    }

    char out[128];
    snprintf(out, sizeof(out), "linkequip slot=%s item=0x%02X equipped=0x%02X", slotName, (unsigned)itemId,
             GET_CUR_FORM_BTN_ITEM(slot));
    Zelda3D_MmLinkReply(output, out);
}

static void Zelda3D_MmLinkState(PlayState* play, Zelda3DMmReplArgs* args, const Zelda3DMmLinkReplOutput* output) {
    Player* player = (play != NULL) ? GET_PLAYER(play) : NULL;
    if (player == NULL) {
        Zelda3D_MmLinkReply(output, "linkstate err (no player)");
        return;
    }

    char state[32] = { 0 };
    char out[256];
    if (!Zelda3D_MmReplNextToken(args, state, sizeof(state)) || !Zelda3D_MmReplArgsEnd(args)) {
        Zelda3D_MmLinkReply(output,
                            "usage: linkstate <idle|walk|run|turn|roll|goronroll|throw|attack|jump|shield|getitem|talk|"
                            "putdown|death|damage|hang|carry|climb|swim|swimdive|itemuse|backwalk|sidestep>");
        return;
    }
    if (strcmp(state, "idle") == 0) {
        Zelda3D_PlayerForceIdle(player, play);
        snprintf(out, sizeof(out), "linkstate idle -> Player_Action_Idle (actionVar1=%d)", player->av1.actionVar1);
    } else if (strcmp(state, "walk") == 0) {
        Zelda3D_PlayerForceWalk(player, play);
        snprintf(out, sizeof(out), "linkstate walk -> Player_Action_13 (speedXZ=%.2f)", player->speedXZ);
    } else if (strcmp(state, "run") == 0) {
        Zelda3D_PlayerForceRun(player, play);
        snprintf(out, sizeof(out), "linkstate run -> Player_Action_14 (speedXZ=%.2f)", player->speedXZ);
    } else if (strcmp(state, "turn") == 0) {
        Zelda3D_PlayerForceTurnInPlace(player, play);
        snprintf(out, sizeof(out), "linkstate turn -> Player_Action_TurnInPlace (turnRate=%d)", player->turnRate);
    } else if (strcmp(state, "roll") == 0) {
        Zelda3D_PlayerForceRoll(player, play);
        snprintf(out, sizeof(out), "linkstate roll -> Player_Action_26 (curFrame=%.2f)", player->skelAnime.curFrame);
    } else if (strcmp(state, "goronroll") == 0) {
        s32 ok = Zelda3D_PlayerForceGoronRoll(player, play);
        snprintf(out, sizeof(out), "linkstate goronroll -> %s (actionVar1=%d speedXZ=%.2f)",
                 ok ? "Player_Action_96" : "requires PLAYER_FORM_GORON", player->av1.actionVar1, player->speedXZ);
    } else if (strcmp(state, "throw") == 0) {
        Zelda3D_PlayerForceThrow(player, play);
        snprintf(out, sizeof(out), "linkstate throw -> Player_Action_42 (PLAYER_ANIMGROUP_throw)");
    } else if (strcmp(state, "attack") == 0) {
        Zelda3D_PlayerForceAttack(player, play);
        snprintf(out, sizeof(out), "linkstate attack -> Player_Action_84 (meleeWeaponAnimation=%d)",
                 player->meleeWeaponAnimation);
    } else if (strcmp(state, "jump") == 0) {
        Zelda3D_PlayerForceJump(player, play);
        snprintf(out, sizeof(out), "linkstate jump -> Player_Action_25 (velocityY=%.2f)", player->actor.velocity.y);
    } else if (strcmp(state, "shield") == 0) {
        Zelda3D_PlayerForceShield(player, play);
        snprintf(out, sizeof(out), "linkstate shield -> Player_Action_18 (stateFlags1=0x%08X)", player->stateFlags1);
    } else if (strcmp(state, "getitem") == 0) {
        Zelda3D_PlayerForceGetItem(player, play);
        snprintf(out, sizeof(out), "linkstate getitem -> Player_Action_WaitForPutAway (stateFlags1=0x%08X)",
                 player->stateFlags1);
    } else if (strcmp(state, "talk") == 0) {
        s32 id = Zelda3D_PlayerForceTalk(player, play, 600.0f);
        snprintf(out, sizeof(out), "linkstate talk -> %s (talkActor textId=0x%x st1=0x%08X)",
                 id ? "talking" : "NO NPC within 600u", player->actor.textId, player->stateFlags1);
    } else if (strcmp(state, "putdown") == 0) {
        Zelda3D_PlayerForcePutDown(player, play);
        snprintf(out, sizeof(out), "linkstate putdown -> Player_Action_41 (PLAYER_ANIMGROUP_put)");
    } else if (strcmp(state, "death") == 0) {
        Zelda3D_PlayerForceDeath(player, play);
        snprintf(out, sizeof(out), "linkstate death -> health=0 (Player_Action_77 entry on a later frame, st1=0x%08X)",
                 player->stateFlags1);
    } else if (strcmp(state, "damage") == 0) {
        Zelda3D_PlayerForceDamage(player, play);
        snprintf(out, sizeof(out), "linkstate damage -> Player_Action_20 (curFrame=%.2f)", player->skelAnime.curFrame);
    } else if (strcmp(state, "hang") == 0) {
        Zelda3D_PlayerForceHang(player, play);
        snprintf(out, sizeof(out), "linkstate hang -> Player_Action_48 (stateFlags1=0x%08X)", player->stateFlags1);
    } else if (strcmp(state, "carry") == 0) {
        s32 ok = Zelda3D_PlayerForceCarry(player, play);
        snprintf(out, sizeof(out), "linkstate carry -> %s (stateFlags1=0x%08X)",
                 ok ? "Player_UpperAction_CarryActor" : "NO heldActor (needs a lifted actor)", player->stateFlags1);
    } else if (strcmp(state, "climb") == 0) {
        s32 entered = Zelda3D_PlayerForceClimb(player, play);
        snprintf(out, sizeof(out), "linkstate climb -> Player_Action_50 (%s, av1=%d st1=0x%08X)",
                 entered == 1   ? "entered"
                 : entered == 0 ? "declined"
                                : "no wallPoly",
                 player->av1.actionVar1, player->stateFlags1);
    } else if (strcmp(state, "swim") == 0) {
        Zelda3D_PlayerForceSwim(player, play);
        snprintf(out, sizeof(out), "linkstate swim -> Player_Action_54 (depthInWater=%.2f)",
                 player->actor.depthInWater);
    } else if (strcmp(state, "swimdive") == 0) {
        Zelda3D_PlayerForceSwimDive(player, play);
        snprintf(out, sizeof(out), "linkstate swimdive -> Player_Action_59 (av2=%d st1=0x%08X st2=0x%08X)",
                 player->av2.actionVar2, player->stateFlags1, player->stateFlags2);
    } else if (strcmp(state, "itemuse") == 0) {
        Zelda3D_PlayerForceItemUse(player, play);
        snprintf(out, sizeof(out), "linkstate itemuse -> Player_Action_68 (av2.actionVar2=%d)", player->av2.actionVar2);
    } else if (strcmp(state, "backwalk") == 0) {
        s32 ok = Zelda3D_PlayerForceBackwalk(player, play);
        snprintf(out, sizeof(out), "linkstate backwalk -> %s (speedXZ=%.2f)",
                 ok ? "Player_Action_15" : "decode declined", player->speedXZ);
    } else if (strcmp(state, "sidestep") == 0) {
        Zelda3D_PlayerForceSidestep(player, play);
        snprintf(out, sizeof(out), "linkstate sidestep -> Player_Action_9 (side_walkR; Z-target-gated)");
    } else {
        snprintf(out, sizeof(out),
                 "usage: linkstate <idle|walk|run|turn|roll|goronroll|throw|attack|jump|shield|getitem|talk|"
                 "putdown|death|damage|hang|carry|climb|swim|swimdive|itemuse|backwalk|sidestep>");
    }
    Zelda3D_MmLinkReply(output, out);
}

s32 Zelda3D_MmLinkReplDispatch(PlayState* play, const char* command, Zelda3DMmReplReply reply, void* user) {
    Zelda3DMmLinkReplOutput output = { reply, user };
    Zelda3DMmReplArgs args;
    if (Zelda3D_MmReplMatch(command, "linkinfo", &args)) {
        if (!Zelda3D_MmReplArgsEnd(&args)) {
            Zelda3D_MmLinkReply(&output, "usage: linkinfo");
        } else {
            Zelda3D_MmLinkInfo(play, &output);
        }
        return 1;
    }
    if (Zelda3D_MmReplMatch(command, "linkform", &args)) {
        Zelda3D_MmLinkForm(play, &args, &output);
        return 1;
    }
    if (Zelda3D_MmReplMatch(command, "linkequip", &args)) {
        Zelda3D_MmLinkEquip(play, &args, &output);
        return 1;
    }
    if (Zelda3D_MmReplMatch(command, "linkitem", &args)) {
        Zelda3D_MmLinkItem(play, &args, &output);
        return 1;
    }
    if (Zelda3D_MmReplMatch(command, "linkstate", &args)) {
        Zelda3D_MmLinkState(play, &args, &output);
        return 1;
    }
    return 0;
}
