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
#include "randomizer_item_delivery_hooks.h"
#include "randomizer_requirement_rules.h"

static std::queue<RandomizerCheck> randomizerQueuedChecks;
static RandomizerCheck randomizerQueuedCheck = RC_UNKNOWN_CHECK;
static GetItemEntry randomizerQueuedItemEntry = GET_ITEM_NONE;

void RandomizerItemQueueReset() {
    randomizerQueuedChecks = std::queue<RandomizerCheck>();
    randomizerQueuedCheck = RC_UNKNOWN_CHECK;
    randomizerQueuedItemEntry = GET_ITEM_NONE;
}

bool RandomizerItemQueueIsDrained() {
    return randomizerQueuedChecks.empty() && randomizerQueuedCheck == RC_UNKNOWN_CHECK;
}

GetItemEntry RandomizerItemQueueCurrentEntry() {
    return randomizerQueuedItemEntry;
}

void RandomizerOnFlagSetHandler(int16_t flagType, int16_t flag) {
    // Consume adult trade items
    if (RAND_GET_OPTION(RSK_SHUFFLE_ADULT_TRADE) && flagType == FLAG_RANDOMIZER_INF) {
        switch (flag) {
            case RAND_INF_ADULT_TRADES_DMT_TRADE_BROKEN_SWORD:
                Flags_UnsetRandomizerInf(RAND_INF_ADULT_TRADES_HAS_SWORD_BROKEN);
                Inventory_ReplaceItem(gPlayState, ITEM_SWORD_BROKEN, Randomizer_GetNextAdultTradeItem());
                break;
            case RAND_INF_ADULT_TRADES_DMT_TRADE_EYEDROPS:
                Flags_UnsetRandomizerInf(RAND_INF_ADULT_TRADES_HAS_EYEDROPS);
                Inventory_ReplaceItem(gPlayState, ITEM_EYEDROPS, Randomizer_GetNextAdultTradeItem());
                break;
        }
    }

    if (flagType == FLAG_EVENT_CHECK_INF && flag == EVENTCHKINF_TALON_WOKEN_IN_CASTLE) {
        // remove chicken as this is the only use for it
        Flags_UnsetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_CHICKEN);
    }

    if (flagType == FLAG_EVENT_CHECK_INF && flag == EVENTCHKINF_OBTAINED_ZELDAS_LETTER) {
        Flags_SetRandomizerInf(RAND_INF_ZELDAS_LETTER);
    }

    if (flagType == FLAG_EVENT_CHECK_INF && flag == EVENTCHKINF_TALON_RETURNED_FROM_CASTLE) {
        if (Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_POCKET_EGG)) {
            Flags_SetRandomizerInf(RAND_INF_TALON_SENT_MALON_HOME);
        }
    }

    RandomizerCheck rc = GetRandomizerCheckFromFlag(flagType, flag);
    if (rc == RC_UNKNOWN_CHECK) {
        return;
    }

    if (flagType == FLAG_GS_TOKEN &&
        Rando::Context::GetInstance()->GetOption(RSK_SHUFFLE_TOKENS).Is(RO_TOKENSANITY_OFF)) {
        Rando::Context::GetInstance()->GetItemLocation(rc)->SetCheckStatus(RCSHOW_COLLECTED);
        return;
    }
    auto loc = Rando::Context::GetInstance()->GetItemLocation(rc);
    if (loc == nullptr || loc->HasObtained() || loc->GetPlacedRandomizerGet() == RG_NONE) {
        Rando::Context::GetInstance()->GetItemLocation(rc)->SetCheckStatus(RCSHOW_COLLECTED);
        return;
    }

    SPDLOG_INFO("Queuing RC: {}", static_cast<uint32_t>(rc));
    randomizerQueuedChecks.push(rc);
}

void RandomizerOnSceneFlagSetHandler(int16_t sceneNum, int16_t flagType, int16_t flag) {
    if (flagType == FLAG_SCENE_SWITCH) {
        auto dungeonInfo = Rando::Context::GetInstance()->GetDungeons()->GetDungeonFromScene(sceneNum);
        bool isVanilla = dungeonInfo == nullptr || dungeonInfo->IsVanilla();

        switch (sceneNum) {
            case SCENE_GERUDOS_FORTRESS:
                if (RAND_GET_OPTION(RSK_SHUFFLE_DUNGEON_ENTRANCES).IsNot(RO_DUNGEON_ENTRANCE_SHUFFLE_OFF) &&
                    flag == 0x3A) {
                    Flags_SetRandomizerInf(RAND_INF_GF_GTG_GATE_PERMANENTLY_OPEN);
                }
                break;
            case SCENE_DEKU_TREE:
                if (!isVanilla && flag == 0x27) {
                    Flags_SetRandomizerInf(RAND_INF_DEKU_TREE_MQ_TORCH_SWITCH);
                }
                break;
            case SCENE_DODONGOS_CAVERN:
                if (!isVanilla && flag == 0x25) {
                    Flags_SetRandomizerInf(RAND_INF_DODONGOS_CAVERN_MQ_SILVER_RUPEES);
                }
                break;
            case SCENE_JABU_JABU:
                if (isVanilla && flag == 0x3b) {
                    Flags_SetRandomizerInf(RAND_INF_JABU_JABUS_BELLY_FIRST_SWITCH);
                }
                break;
            case SCENE_FOREST_TEMPLE:
                if (flag == 0x26) {
                    Flags_SetRandomizerInf(RAND_INF_FOREST_DRAINED_WELL);
                } else if (flag == 0x25) {
                    Flags_SetRandomizerInf(RAND_INF_FOREST_LOBBY_EYES);
                    if (!isVanilla) {
                        Flags_SetSwitch(gPlayState, 0x2a);
                    }
                } else if (!isVanilla && flag == 0x2a) {
                    Flags_SetRandomizerInf(RAND_INF_FOREST_LOBBY_EYES);
                    Flags_SetSwitch(gPlayState, 0x25);
                } else if (!isVanilla && flag == 0x21) {
                    Flags_SetRandomizerInf(RAND_INF_FOREST_MQ_COURTYARD_WEB_BURNT);
                }
                break;
            case SCENE_FIRE_TEMPLE:
                if (!isVanilla && flag == 0x28) {
                    Flags_SetRandomizerInf(RAND_INF_FIRE_MQ_LOBBY_TORCHES);
                }
                break;
            case SCENE_SPIRIT_TEMPLE:
                if (isVanilla && flag == 0x23) {
                    Flags_SetRandomizerInf(RAND_INF_SPIRIT_SUN_ON_FLOOR_ON);
                } else if (!isVanilla && flag == 0x37) {
                    Flags_SetRandomizerInf(RAND_INF_SPIRIT_MQ_LOBBY_SILVER_RUPEES);
                }
                break;
        }
    }

    RandomizerCheck rc = GetRandomizerCheckFromSceneFlag(sceneNum, flagType, flag);
    if (rc == RC_UNKNOWN_CHECK) {
        return;
    }

    auto loc = Rando::Context::GetInstance()->GetItemLocation(rc);
    if (loc == nullptr || loc->HasObtained() || loc->GetPlacedRandomizerGet() == RG_NONE) {
        return;
    }

    SPDLOG_INFO("Queuing RC: {}", static_cast<uint32_t>(rc));
    randomizerQueuedChecks.push(rc);
}

static Vec3f spawnPos = { 0.0f, -999.0f, 0.0f };

void RandomizerOnPlayerUpdateForRCQueueHandler() {
    // If we're already queued, don't queue again
    if (randomizerQueuedCheck != RC_UNKNOWN_CHECK) {
        return;
    }

    // If there's nothing to queue, don't queue
    if (randomizerQueuedChecks.size() < 1) {
        return;
    }

    // If we're in a cutscene, don't queue
    Player* player = GET_PLAYER(gPlayState);
    if (Player_InBlockingCsMode(gPlayState, player) || player->stateFlags1 & PLAYER_STATE1_IN_ITEM_CS ||
        player->stateFlags1 & PLAYER_STATE1_GETTING_ITEM || player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) {
        return;
    }

    RandomizerCheck rc = randomizerQueuedChecks.front();
    auto loc = Rando::Context::GetInstance()->GetItemLocation(rc);
    RandomizerGet vanillaRandomizerGet = Rando::StaticData::GetLocation(rc)->GetVanillaItem();
    GetItemID vanillaItem = (GetItemID)Rando::StaticData::RetrieveItem(vanillaRandomizerGet).GetItemID();
    GetItemEntry getItemEntry =
        Rando::Context::GetInstance()->GetFinalGIEntry(rc, true, (GetItemID)vanillaRandomizerGet);
    GetItemCategory getItemCategory = Randomizer_AdjustItemCategory(getItemEntry);

    if (loc->HasObtained()) {
        SPDLOG_INFO("RC {} already obtained, skipping", static_cast<uint32_t>(rc));
    } else {
        iceTrapScale = 0.0f;
        randomizerQueuedCheck = rc;
        randomizerQueuedItemEntry = getItemEntry;
        SPDLOG_INFO("Queuing Item mod {} item {} from RC {}", getItemEntry.modIndex, getItemEntry.itemId,
                    static_cast<uint32_t>(rc));
        if (
            // Skipping ItemGet animation incompatible with checks that require closing a text box to finish
            rc != RC_HF_OCARINA_OF_TIME_ITEM && rc != RC_SPIRIT_TEMPLE_SILVER_GAUNTLETS_CHEST &&
            rc != RC_MARKET_BOMBCHU_BOWLING_FIRST_PRIZE && rc != RC_MARKET_BOMBCHU_BOWLING_SECOND_PRIZE &&
            // Always show ItemGet animation for ice traps
            !(getItemEntry.modIndex == MOD_RANDOMIZER && getItemEntry.getItemId == RG_ICE_TRAP) &&
            // Always show ItemGet animation outside of randomizer to keep behaviour consistent in vanilla
            IS_RANDO &&
            (CVarGetInteger(CVAR_RANDOMIZER_ENHANCEMENT("TimeSavers.SkipGetItemAnimation"), SGIA_JUNK) == SGIA_ALL ||
             (CVarGetInteger(CVAR_RANDOMIZER_ENHANCEMENT("TimeSavers.SkipGetItemAnimation"), SGIA_JUNK) == SGIA_JUNK &&
              (
                  // crude fix to ensure map hints are readable. Ideally replace with better hint tracking.
                  !(getItemEntry.getItemId >= RG_DEKU_TREE_MAP && getItemEntry.getItemId <= RG_ICE_CAVERN_MAP &&
                    getItemEntry.modIndex == MOD_RANDOMIZER) &&
                  (getItemCategory == ITEM_CATEGORY_JUNK || getItemCategory == ITEM_CATEGORY_SKULLTULA_TOKEN ||
                   getItemCategory == ITEM_CATEGORY_HEALTH || getItemCategory == ITEM_CATEGORY_LESSER))))) {
            Item_DropCollectible(gPlayState, &spawnPos, static_cast<int16_t>(ITEM00_SOH_GIVE_ITEM_ENTRY | 0x8000));
        }
    }

    randomizerQueuedChecks.pop();
}

void RandomizerOnPlayerUpdateForItemQueueHandler() {
    if (randomizerQueuedCheck == RC_UNKNOWN_CHECK) {
        return;
    }

    Player* player = GET_PLAYER(gPlayState);
    if (player == NULL || Player_InBlockingCsMode(gPlayState, player) ||
        player->stateFlags1 & PLAYER_STATE1_IN_ITEM_CS || player->stateFlags1 & PLAYER_STATE1_GETTING_ITEM ||
        player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) {
        return;
    }

    SPDLOG_INFO("Attempting to give Item mod {} item {} from RC {}", randomizerQueuedItemEntry.modIndex,
                randomizerQueuedItemEntry.itemId, static_cast<uint32_t>(randomizerQueuedCheck));
    GiveItemEntryWithoutActor(gPlayState, randomizerQueuedItemEntry);
    if (player->stateFlags1 & PLAYER_STATE1_IN_WATER) {
        // Allow the player to receive the item while swimming
        player->stateFlags2 |= PLAYER_STATE2_UNDERWATER;
        Player_ActionHandler_2(player, gPlayState);
    }
}

void RandomizerOnItemReceiveHandler(GetItemEntry receivedItemEntry) {
    if (randomizerQueuedCheck == RC_UNKNOWN_CHECK) {
        return;
    }

    auto loc = Rando::Context::GetInstance()->GetItemLocation(randomizerQueuedCheck);
    if (randomizerQueuedItemEntry.modIndex == receivedItemEntry.modIndex &&
        randomizerQueuedItemEntry.itemId == receivedItemEntry.itemId) {
        SPDLOG_INFO("Item received mod {} item {} from RC {}", receivedItemEntry.modIndex, receivedItemEntry.itemId,
                    static_cast<uint32_t>(randomizerQueuedCheck));
        loc->SetCheckStatus(RCSHOW_COLLECTED);
        CheckTracker::SpoilAreaFromCheck(randomizerQueuedCheck);
        CheckTracker::RecalculateAllAreaTotals();
        CheckTracker::RecalculateAvailableChecks();
        SaveManager::Instance->SaveSection(gSaveContext.fileNum, SECTION_ID_TRACKER_DATA, true);
        randomizerQueuedCheck = RC_UNKNOWN_CHECK;
        randomizerQueuedItemEntry = GET_ITEM_NONE;
    }

    if (receivedItemEntry.modIndex == MOD_NONE) {
        switch (receivedItemEntry.itemId) {
            case ITEM_SHIELD_DEKU:
                Flags_SetRandomizerInf(RAND_INF_HAS_FOUND_DEKU_SHIELD);
                break;
            case ITEM_SHIELD_HYLIAN:
                Flags_SetRandomizerInf(RAND_INF_HAS_FOUND_HYLIAN_SHIELD);
                break;
            case ITEM_TUNIC_GORON:
                Flags_SetRandomizerInf(RAND_INF_HAS_FOUND_GORON_TUNIC);
                break;
            case ITEM_TUNIC_ZORA:
                Flags_SetRandomizerInf(RAND_INF_HAS_FOUND_ZORA_TUNIC);
                break;
        }
    }

    if (receivedItemEntry.modIndex == MOD_RANDOMIZER && receivedItemEntry.getItemId == RG_MAGIC_BEAN_PACK) {
        if (OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_SKIP_PLANTING_BEANS)) {
            gSaveContext.sceneFlags[SCENE_DEATH_MOUNTAIN_CRATER].swch |= (1 << 3);
            if (gPlayState->sceneNum == SCENE_DEATH_MOUNTAIN_CRATER) {
                Flags_SetSwitch(gPlayState, 3);
            }
            gSaveContext.sceneFlags[SCENE_DEATH_MOUNTAIN_TRAIL].swch |= (1 << 6);
            if (gPlayState->sceneNum == SCENE_DEATH_MOUNTAIN_TRAIL) {
                Flags_SetSwitch(gPlayState, 6);
            }
            gSaveContext.sceneFlags[SCENE_DESERT_COLOSSUS].swch |= (1 << 24);
            if (gPlayState->sceneNum == SCENE_DESERT_COLOSSUS) {
                Flags_SetSwitch(gPlayState, 24);
            }
            gSaveContext.sceneFlags[SCENE_GERUDO_VALLEY].swch |= (1 << 3);
            if (gPlayState->sceneNum == SCENE_GERUDO_VALLEY) {
                Flags_SetSwitch(gPlayState, 3);
            }
            gSaveContext.sceneFlags[SCENE_GRAVEYARD].swch |= (1 << 3);
            if (gPlayState->sceneNum == SCENE_GRAVEYARD) {
                Flags_SetSwitch(gPlayState, 3);
            }
            gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].swch |= (1 << 9);
            if (gPlayState->sceneNum == SCENE_KOKIRI_FOREST) {
                Flags_SetSwitch(gPlayState, 9);
            }
            gSaveContext.sceneFlags[SCENE_LAKE_HYLIA].swch |= (1 << 1);
            if (gPlayState->sceneNum == SCENE_LAKE_HYLIA) {
                Flags_SetSwitch(gPlayState, 1);
            }
            gSaveContext.sceneFlags[SCENE_LOST_WOODS].swch |= (1 << 4) | (1 << 18);
            if (gPlayState->sceneNum == SCENE_LOST_WOODS) {
                Flags_SetSwitch(gPlayState, 4);
                Flags_SetSwitch(gPlayState, 18);
            }
            gSaveContext.sceneFlags[SCENE_ZORAS_RIVER].swch |= (1 << 3);
            if (gPlayState->sceneNum == SCENE_ZORAS_RIVER) {
                Flags_SetSwitch(gPlayState, 3);
            }
            ObjBean* bean = (ObjBean*)Actor_Find(&gPlayState->actorCtx, ACTOR_OBJ_BEAN, ACTORCAT_BG);
            if (bean != nullptr) {
                Flags_SetSwitch(gPlayState, bean->dyna.actor.params & 0x3F);
                func_80B8FE00(bean);
            }
            AMMO(ITEM_BEAN) = 0;
        }
    }

    if (receivedItemEntry.modIndex == MOD_NONE && receivedItemEntry.itemId == ITEM_SONG_EPONA) {
        Flags_SetEventChkInf(EVENTCHKINF_EPONA_OBTAINED);
    }

    if (receivedItemEntry.modIndex == MOD_NONE &&
        (receivedItemEntry.itemId == ITEM_HEART_PIECE || receivedItemEntry.itemId == ITEM_HEART_PIECE_2 ||
         receivedItemEntry.itemId == ITEM_HEART_CONTAINER)) {
        gSaveContext.healthAccumulator = MAX_HEALTH; // Refill 20 hearts
        if ((s32)(gSaveContext.inventory.questItems & 0xF0000000) == 0x40000000) {
            gSaveContext.inventory.questItems ^= 0x40000000;
            gSaveContext.healthCapacity += FULL_HEART_HEALTH;
            gSaveContext.health += FULL_HEART_HEALTH;
        }
    }

    if (loc->GetRandomizerCheck() == RC_SPIRIT_TEMPLE_SILVER_GAUNTLETS_CHEST &&
        !CVarGetInteger(CVAR_ENHANCEMENT("TimeSavers.SkipCutscene.Story"), IS_RANDO)) {
        static uint32_t updateHook;
        updateHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>([]() {
            Player* player = GET_PLAYER(gPlayState);
            if (player == NULL || Player_InBlockingCsMode(gPlayState, player) ||
                player->stateFlags1 & PLAYER_STATE1_IN_ITEM_CS || player->stateFlags1 & PLAYER_STATE1_GETTING_ITEM ||
                player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) {
                return;
            }

            gPlayState->nextEntranceIndex = ENTR_DESERT_COLOSSUS_EAST_EXIT;
            gPlayState->transitionTrigger = TRANS_TRIGGER_START;
            gSaveContext.nextCutsceneIndex = 0xFFF1;
            gPlayState->transitionType = TRANS_TYPE_SANDSTORM_END;
            GET_PLAYER(gPlayState)->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
            Player_TryCsAction(gPlayState, NULL, 8);
            GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnPlayerUpdate>(updateHook);
        });
    }
}

void EnExItem_DrawRandomizedItem(EnExItem* enExItem, PlayState* play) {
    GetItemEntry randoGetItem = enExItem->sohItemEntry;
    if (CVarGetInteger(CVAR_RANDOMIZER_ENHANCEMENT("MysteriousShuffle"), 0)) {
        randoGetItem = GET_ITEM_MYSTERY;
    }
    func_8002EBCC(&enExItem->actor, play, 0);
    func_8002ED80(&enExItem->actor, play, 0);
    EnItem00_CustomItemsParticles(&enExItem->actor, play, randoGetItem);
    GetItemEntry_Draw(play, randoGetItem);
}

void EnExItem_WaitForObjectRandomized(EnExItem* enExItem, PlayState* play) {
    EnExItem_WaitForObject(enExItem, play);
    if (Object_IsLoaded(&play->objectCtx, enExItem->objectIdx)) {
        enExItem->actor.draw = (ActorFunc)EnExItem_DrawRandomizedItem;
        Actor_SetScale(&enExItem->actor, enExItem->scale);

        // for now we're just using this to not have items float
        // below the bowling counter, but it would be nice to use
        // this to not draw gigantic skull tokens etc.
        switch (enExItem->type) {
            case EXITEM_BOMB_BAG_COUNTER: {
                enExItem->actor.shape.yOffset = -10.0f;
                break;
            }
        }
    }
}

void EnItem00_DrawRandomizedItem(EnItem00* enItem00, PlayState* play) {
    f32 mtxScale = CVarGetFloat(CVAR_RANDOMIZER_ENHANCEMENT("TimeSavers.SkipGetItemAnimationScale"), 10.0f);
    Matrix_Scale(mtxScale, mtxScale, mtxScale, MTXMODE_APPLY);
    GetItemEntry randoItem = enItem00->itemEntry;
    if (CVarGetInteger(CVAR_RANDOMIZER_ENHANCEMENT("MysteriousShuffle"), 0) &&
        enItem00->actor.params != ITEM00_SOH_GIVE_ITEM_ENTRY) {
        randoItem = GET_ITEM_MYSTERY;
    }
    func_8002EBCC(&enItem00->actor, play, 0);
    func_8002ED80(&enItem00->actor, play, 0);
    EnItem00_CustomItemsParticles(&enItem00->actor, play, randoItem);
    GetItemEntry_Draw(play, randoItem);
}

void ItemBHeart_DrawRandomizedItem(ItemBHeart* itemBHeart, PlayState* play) {
    GetItemEntry randoItem = itemBHeart->sohItemEntry;
    if (CVarGetInteger(CVAR_RANDOMIZER_ENHANCEMENT("MysteriousShuffle"), 0)) {
        randoItem = GET_ITEM_MYSTERY;
    }
    func_8002EBCC(&itemBHeart->actor, play, 0);
    func_8002ED80(&itemBHeart->actor, play, 0);
    EnItem00_CustomItemsParticles(&itemBHeart->actor, play, randoItem);
    GetItemEntry_Draw(play, randoItem);
}

void ItemBHeart_UpdateRandomizedItem(Actor* actor, PlayState* play) {
    ItemBHeart* itemBHeart = (ItemBHeart*)actor;

    func_80B85264(itemBHeart, play);
    Actor_UpdateBgCheckInfo(play, &itemBHeart->actor, 0.0f, 0.0f, 0.0f, 4);
    if ((itemBHeart->actor.xzDistToPlayer < 30.0f) && (fabsf(itemBHeart->actor.yDistToPlayer) < 40.0f)) {
        Flags_SetCollectible(play, 0x1F);
        Actor_Kill(&itemBHeart->actor);
    }
}

void ItemEtcetera_DrawRandomizedItem(ItemEtcetera* itemEtcetera, PlayState* play) {
    GetItemEntry randoItem = itemEtcetera->sohItemEntry;
    if (CVarGetInteger(CVAR_RANDOMIZER_ENHANCEMENT("MysteriousShuffle"), 0)) {
        randoItem = GET_ITEM_MYSTERY;
    }
    EnItem00_CustomItemsParticles(&itemEtcetera->actor, play, randoItem);
    func_8002EBCC(&itemEtcetera->actor, play, 0);
    func_8002ED80(&itemEtcetera->actor, play, 0);
    GetItemEntry_Draw(play, randoItem);
}

void ItemEtcetera_DrawRandomizedItemThroughLens(ItemEtcetera* itemEtcetera, PlayState* play) {
    if (play->actorCtx.lensActive) { // todo [Rando] mysterious shuffle for chest minigame key shuffle
        ItemEtcetera_DrawRandomizedItem(itemEtcetera, play);
    }
}

void ItemEtcetera_func_80B858B4_Randomized(ItemEtcetera* itemEtcetera, PlayState* play) {
    if (itemEtcetera->actor.xzDistToPlayer < 30.0f && fabsf(itemEtcetera->actor.yDistToPlayer) < 50.0f) {
        if ((itemEtcetera->actor.params & 0xFF) == 1) {
            Flags_SetEventChkInf(EVENTCHKINF_OBTAINED_RUTOS_LETTER);
            Flags_SetSwitch(play, 0xB);
        }

        Actor_Kill(&itemEtcetera->actor);
    } else {
        if ((play->gameplayFrames & 0xD) == 0) {
            EffectSsBubble_Spawn(play, &itemEtcetera->actor.world.pos, 0.0f, 0.0f, 10.0f, 0.13f);
        }
    }
}

void ItemEtcetera_func_80B85824_Randomized(ItemEtcetera* itemEtcetera, PlayState* play) {
    if ((itemEtcetera->actor.params & 0xFF) != 7) {
        return;
    }

    if (itemEtcetera->actor.xzDistToPlayer < 30.0f && fabsf(itemEtcetera->actor.yDistToPlayer) < 50.0f) {

        Flags_SetTreasure(play, 0x1F);
        Actor_Kill(&itemEtcetera->actor);
    }
}

void ItemEtcetera_MoveRandomizedFireArrowDown(ItemEtcetera* itemEtcetera, PlayState* play) {
    Actor_UpdateBgCheckInfo(play, &itemEtcetera->actor, 10.0f, 10.0f, 0.0f, 5);
    Actor_MoveXZGravity(&itemEtcetera->actor);
    if (!(itemEtcetera->actor.bgCheckFlags & 1)) {
        ItemEtcetera_SpawnSparkles(itemEtcetera, play);
    }
    itemEtcetera->actor.shape.rot.y += 0x400;
    ItemEtcetera_func_80B85824_Randomized(itemEtcetera, play);
}

void ItemEtcetera_UpdateRandomizedFireArrow(ItemEtcetera* itemEtcetera, PlayState* play) {
    if ((play->csCtx.state != CS_STATE_IDLE) && (play->csCtx.npcActions[0] != NULL)) {
        if (play->csCtx.npcActions[0]->action == 2) {
            itemEtcetera->actor.draw = (ActorFunc)ItemEtcetera_DrawRandomizedItem;
            itemEtcetera->actor.gravity = -0.1f;
            itemEtcetera->actor.minVelocityY = -4.0f;
            itemEtcetera->actionFunc = ItemEtcetera_MoveRandomizedFireArrowDown;
        }
    } else {
        itemEtcetera->actor.gravity = -0.1f;
        itemEtcetera->actor.minVelocityY = -4.0f;
        itemEtcetera->actionFunc = ItemEtcetera_MoveRandomizedFireArrowDown;
    }
}

u8 EnDs_RandoCanGetGrannyItem() {
    return (RAND_GET_OPTION(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL_BUT_BEANS) ||
            RAND_GET_OPTION(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL)) &&
           !Flags_GetRandomizerInf(RAND_INF_MERCHANTS_GRANNYS_SHOP) &&
           // Traded odd mushroom when adult trade is on
           ((RAND_GET_OPTION(RSK_SHUFFLE_ADULT_TRADE) && Flags_GetItemGetInf(ITEMGETINF_30)) ||
            (!RAND_GET_OPTION(RSK_SHUFFLE_ADULT_TRADE) &&
             (RAND_GET_OPTION(RSK_EARLY_GRANNYS_SHOP) || INV_CONTENT(ITEM_CLAIM_CHECK) == ITEM_CLAIM_CHECK)));
}

u8 EnJs_RandoCanGetCarpetMerchantItem() {
    return (RAND_GET_OPTION(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL) ||
            RAND_GET_OPTION(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL_BUT_BEANS)) &&
           // If the rando check has already been awarded, use vanilla behavior.
           !Flags_GetRandomizerInf(RAND_INF_MERCHANTS_CARPET_SALESMAN);
}

u8 EnGm_RandoCanGetMedigoronItem() {
    return (RAND_GET_OPTION(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL) ||
            RAND_GET_OPTION(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL_BUT_BEANS)) &&
           // If the rando check has already been awarded, use vanilla behavior.
           !Flags_GetRandomizerInf(RAND_INF_MERCHANTS_MEDIGORON);
}

void RandomizerSetChestGameRandomizerInf(RandomizerCheck rc) {
    switch (rc) {
        case RC_MARKET_TREASURE_CHEST_GAME_ITEM_1:
            Flags_SetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_ITEM_1);
            break;
        case RC_MARKET_TREASURE_CHEST_GAME_ITEM_2:
            Flags_SetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_ITEM_2);
            break;
        case RC_MARKET_TREASURE_CHEST_GAME_ITEM_3:
            Flags_SetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_ITEM_3);
            break;
        case RC_MARKET_TREASURE_CHEST_GAME_ITEM_4:
            Flags_SetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_ITEM_4);
            break;
        case RC_MARKET_TREASURE_CHEST_GAME_ITEM_5:
            Flags_SetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_ITEM_5);
            break;
        case RC_MARKET_TREASURE_CHEST_GAME_KEY_1:
            Flags_SetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_KEY_1);
            break;
        case RC_MARKET_TREASURE_CHEST_GAME_KEY_2:
            Flags_SetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_KEY_2);
            break;
        case RC_MARKET_TREASURE_CHEST_GAME_KEY_3:
            Flags_SetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_KEY_3);
            break;
        case RC_MARKET_TREASURE_CHEST_GAME_KEY_4:
            Flags_SetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_KEY_4);
            break;
        case RC_MARKET_TREASURE_CHEST_GAME_KEY_5:
            Flags_SetRandomizerInf(RAND_INF_MARKET_TREASURE_CHEST_GAME_KEY_5);
            break;
        default:
            break;
    }
}

void Player_Action_8084E6D4_override(Player* player, PlayState* play) {
    if (LinkAnimation_Update(play, &player->skelAnime)) {
        func_8084DFAC(play, player);
    }
}

void func_8083A434_override(PlayState* play, Player* player) {
    Player_SetupActionPreserveAnimMovement(play, player, Player_Action_8084E6D4_override, 0);
    player->stateFlags1 |= PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_IN_CUTSCENE;
}

bool ShouldGiveFishingPrize(f32 sFishOnHandLength) {
    // RANDOTODO: update the enhancement sliders to not allow
    // values above rando fish weight values when rando'd
    if (LINK_IS_CHILD) {
        int32_t weight = CVarGetInteger(CVAR_ENHANCEMENT("CustomizeFishing"), 0)
                             ? CVarGetInteger(CVAR_ENHANCEMENT("MinimumFishWeightChild"), 10)
                             : 10;
        f32 score = sqrt(((f32)weight - 0.5f) / 0.0036f);
        return sFishOnHandLength >= score && (IS_RANDO ? !Flags_GetRandomizerInf(RAND_INF_CHILD_FISHING)
                                                       : !(HIGH_SCORE(HS_FISHING) & HS_FISH_PRIZE_CHILD));
    } else {
        int32_t weight = CVarGetInteger(CVAR_ENHANCEMENT("CustomizeFishing"), 0)
                             ? CVarGetInteger(CVAR_ENHANCEMENT("MinimumFishWeightAdult"), 13)
                             : 13;
        f32 score = sqrt(((f32)weight - 0.5f) / 0.0036f);
        return sFishOnHandLength >= score && (IS_RANDO ? !Flags_GetRandomizerInf(RAND_INF_ADULT_FISHING)
                                                       : !(HIGH_SCORE(HS_FISHING) & HS_FISH_PRIZE_ADULT));
    }
}
