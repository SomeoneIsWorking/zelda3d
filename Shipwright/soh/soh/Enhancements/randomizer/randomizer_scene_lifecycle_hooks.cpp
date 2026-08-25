#include <libultraship/bridge.h>
#include "soh/OTRGlobals.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/Enhancements/enhancementTypes.h"
#include "soh/Enhancements/custom-message/CustomMessageTypes.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/randomizer/dungeon.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/SohGui/ImGuiUtils.h"
#include "gui/Notification.h"
#include "soh/SaveManager.h"
#include "init/ShipInit.hpp"
#include "object/ObjectExtension.h"
#include "item_category_adj.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/randomizer/RCToRandInf.h"

extern "C" {
#include "macros.h"
#include "functions/actors.h"
#include "functions/animation.h"
#include "functions/audio.h"
#include "functions/effects.h"
#include "functions/game_state.h"
#include "functions/math.h"
#include "functions/player.h"
#include "functions/rendering.h"
#include "functions/ui.h"
#include "variables.h"
#include "soh/Enhancements/randomizer/ShuffleTradeItems.h"
#include "soh/Enhancements/randomizer/randomizer_entrance.h"
#include "soh/Enhancements/randomizer/randomizer_grotto.h"
#include "src/overlays/actors/ovl_Bg_Treemouth/z_bg_treemouth.h"
#include "src/overlays/actors/ovl_Bg_Jya_Bigmirror/z_bg_jya_bigmirror.h"
#include "src/overlays/actors/ovl_En_Si/z_en_si.h"
#include "src/overlays/actors/ovl_En_Ossan/z_en_ossan.h"
#include "src/overlays/actors/ovl_En_Shopnuts/z_en_shopnuts.h"
#include "src/overlays/actors/ovl_En_Dns/z_en_dns.h"
#include "src/overlays/actors/ovl_Item_B_Heart/z_item_b_heart.h"
#include "src/overlays/actors/ovl_En_Ko/z_en_ko.h"
#include "src/overlays/actors/ovl_En_Mk/z_en_mk.h"
#include "src/overlays/actors/ovl_En_Nb/z_en_nb.h"
#include "src/overlays/actors/ovl_En_Niw_Lady/z_en_niw_lady.h"
#include "src/overlays/actors/ovl_En_Kz/z_en_kz.h"
#include "src/overlays/actors/ovl_En_Ms/z_en_ms.h"
#include "src/overlays/actors/ovl_En_Fr/z_en_fr.h"
#include "src/overlays/actors/ovl_En_Syateki_Man/z_en_syateki_man.h"
#include "src/overlays/actors/ovl_En_Sth/z_en_sth.h"
#include "src/overlays/actors/ovl_Item_Etcetera/z_item_etcetera.h"
#include "src/overlays/actors/ovl_En_Box/z_en_box.h"
#include "src/overlays/actors/ovl_En_Skj/z_en_skj.h"
#include "src/overlays/actors/ovl_En_Hy/z_en_hy.h"
#include "src/overlays/actors/ovl_En_Bom_Bowl_Pit/z_en_bom_bowl_pit.h"
#include "src/overlays/actors/ovl_En_Ge1/z_en_ge1.h"
#include "src/overlays/actors/ovl_En_Ge2/z_en_ge2.h"
#include "src/overlays/actors/ovl_En_Ds/z_en_ds.h"
#include "src/overlays/actors/ovl_En_Dnt_Jiji/z_en_dnt_jiji.h"
#include "src/overlays/actors/ovl_En_Gm/z_en_gm.h"
#include "src/overlays/actors/ovl_En_Js/z_en_js.h"
#include "src/overlays/actors/ovl_En_Okarina_Tag/z_en_okarina_tag.h"
#include "src/overlays/actors/ovl_En_Door/z_en_door.h"
#include "src/overlays/actors/ovl_Door_Shutter/z_door_shutter.h"
#include "src/overlays/actors/ovl_Door_Gerudo/z_door_gerudo.h"
#include "src/overlays/actors/ovl_En_Xc/z_en_xc.h"
#include "src/overlays/actors/ovl_Fishing/z_fishing.h"
#include "src/overlays/actors/ovl_Obj_Bean/z_obj_bean.h"
#include "src/overlays/actors/ovl_En_Heishi2/z_en_heishi2.h"
#include "src/overlays/actors/ovl_En_GirlA/z_en_girla.h"
#include "draw.h"

extern SaveContext gSaveContext;
extern PlayState* gPlayState;
extern void func_8084DFAC(PlayState* play, Player* player);
extern void func_80B8FE00(ObjBean*); // trigger planting
extern void Player_SetupActionPreserveAnimMovement(PlayState* play, Player* player, PlayerActionFunc actionFunc,
                                                   s32 flags);
extern s32 Player_SetupWaitForPutAway(PlayState* play, Player* player, AfterPutAwayFunc func);
extern void Play_InitEnvironment(PlayState* play, s16 skyboxId);
extern void EnMk_Wait(EnMk* enMk, PlayState* play);
extern void func_80ABA778(EnNiwLady* enNiwLady, PlayState* play);
extern void EnDntJiji_GivePrize(EnDntJiji* enDntJiji, PlayState* play);
extern void EnGe1_Wait_Archery(EnGe1* enGe1, PlayState* play);
extern void EnGe1_SetAnimationIdle(EnGe1* enGe1);
extern void EnGe1_SetAnimationIdle(EnGe1* enGe1);
extern void EnGe2_SetupCapturePlayer(EnGe2* enGe2, PlayState* play);
}
#include "randomizer_scene_lifecycle_hooks.h"

void RandomizerOnSceneInitHandler(int16_t sceneNum) {
    // Treasure Chest Game
    // todo: for now we're just unsetting all of them, we will
    //       probably need to do something different when we implement shuffle
    if (sceneNum == SCENE_TREASURE_BOX_SHOP) {
        Flags_UnsetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_ITEM_1);
        Rando::Context::GetInstance()
            ->GetItemLocation(RC_MARKET_TREASURE_CHEST_GAME_ITEM_1)
            ->SetCheckStatus(RCSHOW_UNCHECKED);
        Flags_UnsetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_ITEM_2);
        Rando::Context::GetInstance()
            ->GetItemLocation(RC_MARKET_TREASURE_CHEST_GAME_ITEM_2)
            ->SetCheckStatus(RCSHOW_UNCHECKED);
        Flags_UnsetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_ITEM_3);
        Rando::Context::GetInstance()
            ->GetItemLocation(RC_MARKET_TREASURE_CHEST_GAME_ITEM_3)
            ->SetCheckStatus(RCSHOW_UNCHECKED);
        Flags_UnsetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_ITEM_4);
        Rando::Context::GetInstance()
            ->GetItemLocation(RC_MARKET_TREASURE_CHEST_GAME_ITEM_4)
            ->SetCheckStatus(RCSHOW_UNCHECKED);
        Flags_UnsetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_ITEM_5);
        Rando::Context::GetInstance()
            ->GetItemLocation(RC_MARKET_TREASURE_CHEST_GAME_ITEM_5)
            ->SetCheckStatus(RCSHOW_UNCHECKED);
        Flags_UnsetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_KEY_1);
        Rando::Context::GetInstance()
            ->GetItemLocation(RC_MARKET_TREASURE_CHEST_GAME_KEY_1)
            ->SetCheckStatus(RCSHOW_UNCHECKED);
        Flags_UnsetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_KEY_2);
        Rando::Context::GetInstance()
            ->GetItemLocation(RC_MARKET_TREASURE_CHEST_GAME_KEY_2)
            ->SetCheckStatus(RCSHOW_UNCHECKED);
        Flags_UnsetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_KEY_3);
        Rando::Context::GetInstance()
            ->GetItemLocation(RC_MARKET_TREASURE_CHEST_GAME_KEY_3)
            ->SetCheckStatus(RCSHOW_UNCHECKED);
        Flags_UnsetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_KEY_4);
        Rando::Context::GetInstance()
            ->GetItemLocation(RC_MARKET_TREASURE_CHEST_GAME_KEY_4)
            ->SetCheckStatus(RCSHOW_UNCHECKED);
        Flags_UnsetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_KEY_5);
        Rando::Context::GetInstance()
            ->GetItemLocation(RC_MARKET_TREASURE_CHEST_GAME_KEY_5)
            ->SetCheckStatus(RCSHOW_UNCHECKED);
        CheckTracker::RecalculateAllAreaTotals();
    }

    // ENTRTODO: Move all entrance rando handling to a dedicated file
    if (RAND_GET_OPTION(RSK_SHUFFLE_ENTRANCES)) {
        // In ER, override roomNum to load based on scene and spawn during scene init
        if (gSaveContext.respawnFlag <= 0) {
            s8 origRoom = gPlayState->roomCtx.curRoom.num;
            s8 replacedRoom = Entrance_OverrideSpawnSceneRoom(gPlayState->sceneNum, gPlayState->curSpawn, origRoom);

            if (origRoom != replacedRoom) {
                // Reset room ctx back to prev room and then load the new room
                gPlayState->roomCtx.status = 0;
                gPlayState->roomCtx.curRoom = gPlayState->roomCtx.prevRoom;
                func_8009728C(gPlayState, &gPlayState->roomCtx, replacedRoom);
            }
        }

        // Handle updated link spawn positions
        Entrance_OverrideSpawnScene(sceneNum, gPlayState->curSpawn);
    }

    // LACS & Prelude checks
    static uint32_t updateHook = 0;

    if (updateHook) {
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnPlayerUpdate>(updateHook);
        updateHook = 0;
    }

    // If we're not in the Temple of Time or we've already learned the Prelude of Light and received LACs, we don't need
    // to do anything
    if (sceneNum != SCENE_TEMPLE_OF_TIME ||
        (Flags_GetEventChkInf(EVENTCHKINF_LEARNED_PRELUDE_OF_LIGHT) &&
         Flags_GetEventChkInf(EVENTCHKINF_RETURNED_TO_TEMPLE_OF_TIME_WITH_ALL_MEDALLIONS))) {
        return;
    }

    updateHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>([]() {
        if (!Flags_GetEventChkInf(EVENTCHKINF_LEARNED_PRELUDE_OF_LIGHT) && LINK_IS_ADULT &&
            CHECK_QUEST_ITEM(QUEST_MEDALLION_FOREST) && gPlayState->roomCtx.curRoom.num == 0) {
            Flags_SetEventChkInf(EVENTCHKINF_LEARNED_PRELUDE_OF_LIGHT);
        }

        // We're always in rando here, and rando always overrides this should so we can just pass false
        if (GameInteractor_Should(VB_BE_ELIGIBLE_FOR_LIGHT_ARROWS, false)) {
            Flags_SetEventChkInf(EVENTCHKINF_RETURNED_TO_TEMPLE_OF_TIME_WITH_ALL_MEDALLIONS);
        }

        // If both awards have been given, we can unregister the hook, otherwise it will get unregistered when the
        // player leaves the area
        if (Flags_GetEventChkInf(EVENTCHKINF_LEARNED_PRELUDE_OF_LIGHT) &&
            Flags_GetEventChkInf(EVENTCHKINF_RETURNED_TO_TEMPLE_OF_TIME_WITH_ALL_MEDALLIONS)) {
            GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnPlayerUpdate>(updateHook);
            updateHook = 0;
        }
    });
}

void RandomizerAfterSceneCommandsHandler(int16_t sceneNum) {
    // ENTRTODO: Move all entrance rando handling to a dedicated file
    if (RAND_GET_OPTION(RSK_SHUFFLE_ENTRANCES)) {
        Entrance_OverrideWeatherState();
    }
}
