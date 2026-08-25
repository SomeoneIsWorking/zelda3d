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
#include "randomizer_actor_lifecycle_hooks.h"
#include "randomizer_item_delivery_hooks.h"
#include "randomizer_requirement_rules.h"
#include "randomizer_scrub_purchase_policy.h"

static ObjectExtension::Register<DnsItemEntry> RegisterDnsItemEntryOverride;

void EnSi_DrawRandomizedItem(EnSi* enSi, PlayState* play) {
    GetItemEntry randoItem = enSi->sohGetItemEntry;
    if (CVarGetInteger(CVAR_RANDOMIZER_ENHANCEMENT("MysteriousShuffle"), 0)) {
        randoItem = GET_ITEM_MYSTERY;
    }
    func_8002ED80(&enSi->actor, play, 0);
    func_8002EBCC(&enSi->actor, play, 0);
    EnItem00_CustomItemsParticles(&enSi->actor, play, randoItem);
    GetItemEntry_Draw(play, randoItem);
}

u32 EnDns_RandomizerPurchaseableCheck(EnDns* enDns) {
    auto checkIdentity = ObjectExtension::GetInstance().Get<ScrubIdentity>(enDns);
    if (checkIdentity != nullptr && Flags_GetRandomizerInf(checkIdentity->identity.randomizerInf)) {
        return DNS_CANBUY_RESULT_CANT_GET_NOW;
    }
    if (gSaveContext.rupees < enDns->dnsItemEntry->itemPrice) {
        return DNS_CANBUY_RESULT_NEED_RUPEES;
    }
    return DNS_CANBUY_RESULT_SUCCESS;
}

void EnDns_RandomizerPurchase(EnDns* enDns) {
    Rupees_ChangeBy(-enDns->dnsItemEntry->itemPrice);
    auto checkIdentity = ObjectExtension::GetInstance().Get<ScrubIdentity>(enDns);
    if (checkIdentity != nullptr) {
        Flags_SetRandomizerInf(checkIdentity->identity.randomizerInf);
    }
}

void RandomizerOnActorInitHandler(void* actorRef) {
    Actor* actor = static_cast<Actor*>(actorRef);

    if (actor->id == ACTOR_PLAYER) {
        auto dungeonInfo = Rando::Context::GetInstance()->GetDungeons()->GetDungeonFromScene(gPlayState->sceneNum);
        bool isVanilla = dungeonInfo == nullptr || dungeonInfo->IsVanilla();
        switch (gPlayState->sceneNum) {
            case SCENE_DEKU_TREE:
                if (!isVanilla && Flags_GetRandomizerInf(RAND_INF_DEKU_TREE_MQ_TORCH_SWITCH)) {
                    Flags_SetSwitch(gPlayState, 0x27);
                }
                if (isVanilla) { // make falling platform respawn
                    Flags_UnsetSwitch(gPlayState, 0x14);
                }
                break;
            case SCENE_DODONGOS_CAVERN:
                if (!isVanilla && Flags_GetRandomizerInf(RAND_INF_DODONGOS_CAVERN_MQ_SILVER_RUPEES)) {
                    Flags_SetSwitch(gPlayState, 0x25);
                }
                if (isVanilla) { // make gossip stone fairy temp flag
                    Flags_UnsetSwitch(gPlayState, 0x11);
                }
                break;
            case SCENE_JABU_JABU:
                if (isVanilla && Flags_GetRandomizerInf(RAND_INF_JABU_JABUS_BELLY_FIRST_SWITCH)) {
                    Flags_SetSwitch(gPlayState, 0x3b);
                }
                break;
            case SCENE_FOREST_TEMPLE:
                if (Flags_GetRandomizerInf(RAND_INF_FOREST_DRAINED_WELL)) {
                    Flags_SetSwitch(gPlayState, 0x26);
                }
                if (Flags_GetRandomizerInf(RAND_INF_FOREST_LOBBY_EYES)) {
                    Flags_SetSwitch(gPlayState, 0x25);
                    if (!isVanilla) {
                        Flags_SetSwitch(gPlayState, 0x2a);
                    }
                }
                if (!isVanilla && Flags_GetRandomizerInf(RAND_INF_FOREST_MQ_COURTYARD_WEB_BURNT)) {
                    Flags_SetSwitch(gPlayState, 0x21);
                }
                break;
            case SCENE_FIRE_TEMPLE:
                if (!isVanilla && Flags_GetRandomizerInf(RAND_INF_FIRE_MQ_LOBBY_TORCHES)) {
                    Flags_SetSwitch(gPlayState, 0x28);
                }
                break;
            case SCENE_SPIRIT_TEMPLE:
                if (isVanilla && Flags_GetRandomizerInf(RAND_INF_SPIRIT_SUN_ON_FLOOR_ON)) {
                    Flags_SetSwitch(gPlayState, 0x23);
                }
                if (!isVanilla && Flags_GetRandomizerInf(RAND_INF_SPIRIT_MQ_LOBBY_SILVER_RUPEES)) {
                    Flags_SetSwitch(gPlayState, 0x37);
                }
                break;
        }
    }

    if (actor->id == ACTOR_EN_SI) {
        RandomizerCheck rc =
            OTRGlobals::Instance->gRandomizer->GetCheckFromActor(actor->id, gPlayState->sceneNum, actor->params);
        if (rc != RC_UNKNOWN_CHECK) {
            EnSi* enSi = static_cast<EnSi*>(actorRef);
            enSi->sohGetItemEntry = Rando::Context::GetInstance()->GetFinalGIEntry(
                rc, true, (GetItemID)Rando::StaticData::GetLocation(rc)->GetVanillaItem());
            actor->draw = (ActorFunc)EnSi_DrawRandomizedItem;
        }
    }

    if (actor->id == ACTOR_EN_DNS) {
        EnDns* enDns = static_cast<EnDns*>(actorRef);
        s16 respawnData = gSaveContext.respawn[RESPAWN_MODE_RETURN].data & ((1 << 8) - 1);
        auto scrubIdentity = IdentifyScrub(gPlayState->sceneNum, enDns->actor.params, respawnData);

        if (scrubIdentity.identity.randomizerCheck != RC_UNKNOWN_CHECK) {
            // DNS uses pointers so we're creating our own entry instead of modifying the original
            ObjectExtension::GetInstance().Set<DnsItemEntry>(actorRef, std::move(DnsItemEntry{
                                                                           enDns->dnsItemEntry->itemPrice,
                                                                           1,
                                                                           scrubIdentity.getItemId,
                                                                           EnDns_RandomizerPurchaseableCheck,
                                                                           EnDns_RandomizerPurchase,
                                                                       }));
            enDns->dnsItemEntry = ObjectExtension::GetInstance().Get<DnsItemEntry>(actorRef);

            if (scrubIdentity.itemPrice != -1) {
                enDns->dnsItemEntry->itemPrice = scrubIdentity.itemPrice;
            }

            ObjectExtension::GetInstance().Set<ScrubIdentity>(actorRef, std::move(scrubIdentity));
            enDns->actor.textId = TEXT_SCRUB_RANDOM;

            static uint32_t enDnsUpdateHook = 0;
            static uint32_t enDnsKillHook = 0;
            if (!enDnsUpdateHook) {
                enDnsUpdateHook =
                    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorUpdate>([](void* innerActorRef) {
                        Actor* innerActor = static_cast<Actor*>(innerActorRef);
                        if (innerActor->id == ACTOR_EN_DNS) {
                            if (ObjectExtension::GetInstance().Has<ScrubIdentity>(innerActor)) {
                                innerActor->textId = TEXT_SCRUB_RANDOM;
                            }
                        }
                    });
                enDnsKillHook =
                    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t sceneNum) {
                        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnActorUpdate>(enDnsUpdateHook);
                        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnSceneInit>(enDnsKillHook);
                        enDnsUpdateHook = 0;
                        enDnsKillHook = 0;
                    });
            }
        }
    }

    if (actor->id == ACTOR_ITEM_ETCETERA) {
        ItemEtcetera* itemEtcetera = static_cast<ItemEtcetera*>(actorRef);
        RandomizerCheck rc = OTRGlobals::Instance->gRandomizer->GetCheckFromActor(
            itemEtcetera->actor.id, gPlayState->sceneNum, itemEtcetera->actor.params);
        if (rc != RC_UNKNOWN_CHECK) {
            itemEtcetera->sohItemEntry = Rando::Context::GetInstance()->GetFinalGIEntry(
                rc, true, (GetItemID)Rando::StaticData::GetLocation(rc)->GetVanillaItem());
            itemEtcetera->drawFunc = (ActorFunc)ItemEtcetera_DrawRandomizedItem;
        }

        int32_t type = itemEtcetera->actor.params & 0xFF;
        switch (type) {
            case ITEM_ETC_LETTER: {
                itemEtcetera->futureActionFunc = (ItemEtceteraActionFunc)ItemEtcetera_func_80B858B4_Randomized;
                break;
            }
            case ITEM_ETC_ARROW_FIRE: {
                itemEtcetera->futureActionFunc = (ItemEtceteraActionFunc)ItemEtcetera_UpdateRandomizedFireArrow;
                break;
            }
            case ITEM_ETC_RUPEE_GREEN_CHEST_GAME:
            case ITEM_ETC_RUPEE_BLUE_CHEST_GAME:
            case ITEM_ETC_RUPEE_RED_CHEST_GAME:
            case ITEM_ETC_RUPEE_PURPLE_CHEST_GAME:
            case ITEM_ETC_HEART_PIECE_CHEST_GAME:
            case ITEM_ETC_KEY_SMALL_CHEST_GAME: {
                if (rc != RC_UNKNOWN_CHECK) {
                    itemEtcetera->drawFunc = (ActorFunc)ItemEtcetera_DrawRandomizedItemThroughLens;
                }
                break;
            }
        }
    }

    if (actor->id == ACTOR_EN_EX_ITEM) {
        EnExItem* enExItem = static_cast<EnExItem*>(actorRef);

        RandomizerCheck rc = RC_UNKNOWN_CHECK;
        switch (enExItem->type) {
            case EXITEM_BOMB_BAG_COUNTER:
            case EXITEM_BOMB_BAG_BOWLING:
                rc = RC_MARKET_BOMBCHU_BOWLING_FIRST_PRIZE;
                break;
            case EXITEM_HEART_PIECE_COUNTER:
            case EXITEM_HEART_PIECE_BOWLING:
                rc = RC_MARKET_BOMBCHU_BOWLING_SECOND_PRIZE;
                break;
            case EXITEM_BOMBCHUS_COUNTER:
            case EXITEM_BOMBCHUS_BOWLING:
                // RC_MARKET_BOMBCHU_BOWLING_BOMBCHUS was removed as a 3DS holdover not in anyones near term plans due
                // to being pretty useless.
                break;
            case EXITEM_BULLET_BAG:
                rc = RC_LW_TARGET_IN_WOODS;
                break;
        }
        if (rc != RC_UNKNOWN_CHECK) {
            enExItem->sohItemEntry = Rando::Context::GetInstance()->GetFinalGIEntry(
                rc, true, (GetItemID)Rando::StaticData::GetLocation(rc)->GetVanillaItem());
            enExItem->actionFunc = (EnExItemActionFunc)EnExItem_WaitForObjectRandomized;
        }
    }

    if (actor->id == ACTOR_EN_GE1) {
        EnGe1* enGe1 = static_cast<EnGe1*>(actorRef);
        auto ge1Type = enGe1->actor.params & 0xFF;
        if (ge1Type == GE1_TYPE_TRAINING_GROUND_GUARD &&
            Flags_GetRandomizerInf(RAND_INF_GF_GTG_GATE_PERMANENTLY_OPEN)) {
            enGe1->actionFunc = (EnGe1ActionFunc)EnGe1_SetNormalText;
        } else if (ge1Type == GE1_TYPE_GATE_OPERATOR && enGe1->actor.world.pos.x != -1358.0f) {
            // When spawning the gate operator, also spawn an extra gate operator on the wasteland side
            Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_GE1, -1358.0f, 88.0f, -3018.0f, 0,
                        static_cast<s16>(0x95B0), 0, 0x0300 | GE1_TYPE_GATE_OPERATOR);
        }
    }

    if (actor->id == ACTOR_BG_JYA_BIGMIRROR && Flags_GetRandomizerInf(RAND_INF_SPIRIT_BIG_MIRROR_STATUE_TURNED)) {
        Flags_SetSwitch(gPlayState, 0x29); // destroy wall
        auto jyaBigMirror = static_cast<BgJyaBigmirror*>(actorRef);
        jyaBigMirror->puzzleFlags |=
            BIGMIR_PUZZLE_COBRA1_SOLVED | BIGMIR_PUZZLE_COBRA2_SOLVED | BIGMIR_PUZZLE_BOMBIWA_DESTROYED;
        jyaBigMirror->cobraInfo[0].rotY = static_cast<s16>(0x4000);
        jyaBigMirror->cobraInfo[1].rotY = static_cast<s16>(0x8000);
    }

    if (actor->id == ACTOR_DEMO_KEKKAI && actor->params == 0) { // 0 == KEKKAI_TOWER
        if (CompletedAllTrials()) {
            Actor_Kill(actor);
        }
    }

    if (actor->id == ACTOR_EN_OSSAN && actor->params == OSSAN_TYPE_MASK &&
        RAND_GET_OPTION(RSK_MASK_QUEST).Is(RO_MASK_QUEST_SHUFFLE)) {
        Actor_Kill(actor);
    }

    if (actor->id == ACTOR_BG_TREEMOUTH && LINK_IS_ADULT &&
        RAND_GET_OPTION(RSK_SHUFFLE_DUNGEON_ENTRANCES).IsNot(RO_DUNGEON_ENTRANCE_SHUFFLE_OFF) &&
        (RAND_GET_OPTION(RSK_FOREST).Is(RO_CLOSED_FOREST_OFF) ||
         Flags_GetEventChkInf(EVENTCHKINF_SHOWED_MIDO_SWORD_SHIELD))) {
        BgTreemouth* bgTreemouth = static_cast<BgTreemouth*>(actorRef);
        bgTreemouth->unk_168 = 1.0f;
    }

    // consumable bags
    if (actor->id == ACTOR_EN_ITEM00 &&
        ((RAND_GET_OPTION(RSK_SHUFFLE_DEKU_STICK_BAG) && CUR_UPG_VALUE(UPG_STICKS) == 0 &&
          actor->params == ITEM00_STICK) ||
         (RAND_GET_OPTION(RSK_SHUFFLE_DEKU_NUT_BAG) && CUR_UPG_VALUE(UPG_NUTS) == 0 && actor->params == ITEM00_NUTS))) {
        Actor_Kill(actor);
    }

    if (RAND_GET_OPTION(RSK_SHUFFLE_BOSS_SOULS)) {
        // Boss souls require an additional item (represented by a RAND_INF) to spawn a boss in a particular lair
        RandomizerInf currentBossSoulRandInf = RAND_INF_MAX;
        switch (gPlayState->sceneNum) {
            case SCENE_DEKU_TREE_BOSS:
                currentBossSoulRandInf = RAND_INF_GOHMA_SOUL;
                break;
            case SCENE_DODONGOS_CAVERN_BOSS:
                currentBossSoulRandInf = RAND_INF_KING_DODONGO_SOUL;
                break;
            case SCENE_JABU_JABU_BOSS:
                currentBossSoulRandInf = RAND_INF_BARINADE_SOUL;
                break;
            case SCENE_FOREST_TEMPLE_BOSS:
                currentBossSoulRandInf = RAND_INF_PHANTOM_GANON_SOUL;
                break;
            case SCENE_FIRE_TEMPLE_BOSS:
                currentBossSoulRandInf = RAND_INF_VOLVAGIA_SOUL;
                break;
            case SCENE_WATER_TEMPLE_BOSS:
                currentBossSoulRandInf = RAND_INF_MORPHA_SOUL;
                break;
            case SCENE_SHADOW_TEMPLE_BOSS:
                currentBossSoulRandInf = RAND_INF_BONGO_BONGO_SOUL;
                break;
            case SCENE_SPIRIT_TEMPLE_BOSS:
                currentBossSoulRandInf = RAND_INF_TWINROVA_SOUL;
                break;
            case SCENE_GANONDORF_BOSS:
            case SCENE_GANON_BOSS:
                if (RAND_GET_OPTION(RSK_SHUFFLE_BOSS_SOULS).Is(RO_BOSS_SOULS_ON_PLUS_GANON)) {
                    currentBossSoulRandInf = RAND_INF_GANON_SOUL;
                }
                break;
            default:
                break;
        }

        // Deletes all actors in the boss category if the soul isn't found.
        // Some actors, like Dark Link, Arwings, and Zora's Sapphire...?, are in this category despite not being actual
        // bosses, so ignore any "boss" if `currentBossSoulRandInf` doesn't change from RAND_INF_MAX. Iron Knuckle
        // (Nabooru) in Twinrova's room is a special exception, so exclude knuckles too.
        if (currentBossSoulRandInf != RAND_INF_MAX) {
            if (!Flags_GetRandomizerInf(currentBossSoulRandInf) && actor->category == ACTORCAT_BOSS &&
                actor->id != ACTOR_EN_IK) {
                Actor_Delete(&gPlayState->actorCtx, actor, gPlayState);
            }
            // Special case for Phantom Ganon's horse (and fake), as they're considered "background actors",
            // but still control the boss fight flow.
            if (!Flags_GetRandomizerInf(RAND_INF_PHANTOM_GANON_SOUL) && actor->id == ACTOR_EN_FHG) {
                Actor_Delete(&gPlayState->actorCtx, actor, gPlayState);
            }
        }
    }

    // In MQ Spirit, remove the large silver block in the hole as child so the chest in the silver block hallway
    // can be guaranteed accessible
    if (actor->id == ACTOR_OBJ_OSHIHIKI && LINK_IS_CHILD && ResourceMgr_IsGameMasterQuest() &&
        gPlayState->sceneNum == SCENE_SPIRIT_TEMPLE && actor->room == 6 && // Spirit Temple silver block hallway
        actor->params == 0x9C7                                             // Silver block that is marked as in the hole
    ) {
        Actor_Kill(actor);
        return;
    }

    if (RAND_GET_OPTION(RSK_SHUFFLE_BEAN_SOULS)) {
        RandomizerInf currentBeanSoulRandInf = RAND_INF_MAX;
        if (actor->id == ACTOR_OBJ_BEAN) {
            switch (gPlayState->sceneNum) {
                case SCENE_DEATH_MOUNTAIN_CRATER:
                    currentBeanSoulRandInf = RAND_INF_DEATH_MOUNTAIN_CRATER_BEAN_SOUL;
                    break;
                case SCENE_DEATH_MOUNTAIN_TRAIL:
                    currentBeanSoulRandInf = RAND_INF_DEATH_MOUNTAIN_TRAIL_BEAN_SOUL;
                    break;
                case SCENE_DESERT_COLOSSUS:
                    currentBeanSoulRandInf = RAND_INF_DESERT_COLOSSUS_BEAN_SOUL;
                    break;
                case SCENE_GERUDO_VALLEY:
                    currentBeanSoulRandInf = RAND_INF_GERUDO_VALLEY_BEAN_SOUL;
                    break;
                case SCENE_GRAVEYARD:
                    currentBeanSoulRandInf = RAND_INF_GRAVEYARD_BEAN_SOUL;
                    break;
                case SCENE_KOKIRI_FOREST:
                    currentBeanSoulRandInf = RAND_INF_KOKIRI_FOREST_BEAN_SOUL;
                    break;
                case SCENE_LAKE_HYLIA:
                    currentBeanSoulRandInf = RAND_INF_LAKE_HYLIA_BEAN_SOUL;
                    break;
                case SCENE_LOST_WOODS:
                    if ((actor->params & 0x3F) == 4) {
                        currentBeanSoulRandInf = RAND_INF_LOST_WOODS_BRIDGE_BEAN_SOUL;
                    } else {
                        currentBeanSoulRandInf = RAND_INF_LOST_WOODS_BEAN_SOUL;
                    }
                    break;
                case SCENE_ZORAS_RIVER:
                    currentBeanSoulRandInf = RAND_INF_ZORAS_RIVER_BEAN_SOUL;
                    break;
            }
        } else if (actor->id == ACTOR_OBJ_MAKEKINSUTA) {
            switch (gPlayState->sceneNum) {
                case SCENE_DEATH_MOUNTAIN_CRATER:
                    currentBeanSoulRandInf = RAND_INF_DEATH_MOUNTAIN_CRATER_BEAN_SOUL;
                    break;
                case SCENE_DEATH_MOUNTAIN_TRAIL:
                    currentBeanSoulRandInf = RAND_INF_DEATH_MOUNTAIN_TRAIL_BEAN_SOUL;
                    break;
                case SCENE_DESERT_COLOSSUS:
                    currentBeanSoulRandInf = RAND_INF_DESERT_COLOSSUS_BEAN_SOUL;
                    break;
                case SCENE_GERUDO_VALLEY:
                    currentBeanSoulRandInf = RAND_INF_GERUDO_VALLEY_BEAN_SOUL;
                    break;
                case SCENE_GRAVEYARD:
                    currentBeanSoulRandInf = RAND_INF_GRAVEYARD_BEAN_SOUL;
                    break;
                case SCENE_KOKIRI_FOREST:
                    currentBeanSoulRandInf = RAND_INF_KOKIRI_FOREST_BEAN_SOUL;
                    break;
                case SCENE_LAKE_HYLIA:
                    currentBeanSoulRandInf = RAND_INF_LAKE_HYLIA_BEAN_SOUL;
                    break;
                case SCENE_LOST_WOODS:
                    if (actor->params == 0x4e01) {
                        currentBeanSoulRandInf = RAND_INF_LOST_WOODS_BRIDGE_BEAN_SOUL;
                    } else {
                        currentBeanSoulRandInf = RAND_INF_LOST_WOODS_BEAN_SOUL;
                    }
                    break;
                case SCENE_ZORAS_RIVER:
                    currentBeanSoulRandInf = RAND_INF_ZORAS_RIVER_BEAN_SOUL;
                    break;
            }
        }
        if (currentBeanSoulRandInf != RAND_INF_MAX && !Flags_GetRandomizerInf(currentBeanSoulRandInf)) {
            Actor_Kill(actor);
            return;
        }
    }

    // If child is in the adult shooting gallery or adult in the child shooting gallery, then despawn the shooting
    // gallery man
    if (actor->id == ACTOR_EN_SYATEKI_MAN && RAND_GET_OPTION(RSK_SHUFFLE_INTERIOR_ENTRANCES) &&
        ((LINK_IS_CHILD &&
          // Kakariko Village -> Adult Shooting Gallery, index 003B in the entrance table
          Entrance_SceneAndSpawnAre(SCENE_SHOOTING_GALLERY, 0x00)) ||
         (LINK_IS_ADULT &&
          // Market -> Child Shooting Gallery,           index 016D in the entrance table
          Entrance_SceneAndSpawnAre(SCENE_SHOOTING_GALLERY, 0x01)))) {
        Actor_Kill(actor);
        return;
    }

    if (actor->id == ACTOR_EN_NB && (actor->params & 0xFF) == NB_TYPE_CRAWLSPACE &&
        !RAND_GET_OPTION(RSK_SHUFFLE_SPEAK)) {
        Actor_Kill(actor);
    }

    // Turn MQ switch into toggle
    if (actor->id == ACTOR_OBJ_SWITCH && gPlayState->sceneNum == SCENE_BOTTOM_OF_THE_WELL &&
        (actor->params & 0x3f07) == 0x303) {
        auto dungeon =
            OTRGlobals::Instance->gRandoContext->GetDungeons()->GetDungeonFromScene(SCENE_BOTTOM_OF_THE_WELL);
        if (dungeon->IsMQ()) {
            actor->params |= 0x10;
        }
    }

    // In ER, once Link has spawned we know the scene has loaded, so we can sanitize the last known entrance type
    if (actor->id == ACTOR_PLAYER && RAND_GET_OPTION(RSK_SHUFFLE_ENTRANCES)) {
        Grotto_SanitizeEntranceType();
    }
}
