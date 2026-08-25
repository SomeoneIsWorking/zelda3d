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
#include "randomizer_vanilla_behavior_policy.h"
#include "randomizer_item_delivery_hooks.h"
#include "randomizer_requirement_rules.h"
#include "randomizer_scrub_purchase_policy.h"

extern "C" void func_80A5475C(EnHeishi2* CastleGuard, PlayState* play);

// buttonStatus[0] doubles as "B disabled" (BTN_DISABLED == 255 == ITEM_NONE) and as temp-B
// storage during minigames/Epona. We use ITEM_NONE_FE (254) as a sentinel so a swordless rando
// player can be funneled through that same temp-B machinery and restored to an empty B later.
#define SWORDLESS_STATUS ITEM_NONE_FE

// true when a swordless player should be funneled through temporary-B force path
// (so their empty B is treated as "occupied", blocking swordless-on-Epona item glitch).
static bool RandoCanTrackSwordless(PlayState* play) {
    Player* player = GET_PLAYER(play);
    // Child is always assumed swordless until the Kokiri Sword is found; adult only with MS shuffle.
    bool isSwordless = (LINK_IS_CHILD || RAND_GET_OPTION(RSK_SHUFFLE_MASTER_SWORD)) &&
                       gSaveContext.equips.buttonItems[0] == ITEM_NONE && Flags_GetInfTable(INFTABLE_SWORDLESS);
    bool wasSwordlessBefore = gSaveContext.buttonStatus[0] == SWORDLESS_STATUS;
    return isSwordless && !wasSwordlessBefore && !RAND_GET_OPTION(RSK_SWORDLESS_EPONA_ITEMS);
}

void RandomizerOnVanillaBehaviorHandler(GIVanillaBehavior id, bool* should, va_list originalArgs) {
    va_list args;
    va_copy(args, originalArgs);

    switch (id) {
        case VB_CLIMB:
            if (RAND_GET_OPTION(RSK_SHUFFLE_CLIMB) && !Flags_GetRandomizerInf(RAND_INF_CAN_CLIMB)) {
                s32* x = va_arg(args, s32*);
                s32* y = va_arg(args, s32*);

                *x = 0;
                if (*y > 0) {
                    *y = 0;
                }
            }
            break;
        case VB_CRAWL:
            *should = *should && Flags_GetRandomizerInf(RAND_INF_CAN_CRAWL);
            break;
        case VB_CAN_BUY_SHOP_SHIELD_OR_TUNIC: {
            // Gate non-randomized shop shields/tunics behind finding a non-shop copy.
            if (RAND_GET_OPTION(RSK_SHOP_SHIELDS_AND_TUNICS_ONLY_REFILL).Is(RO_GENERIC_ON)) {
                EnGirlACanBuyResult* canBuy = va_arg(args, EnGirlACanBuyResult*);
                RandomizerInf requiredInf = (RandomizerInf)va_arg(args, int);
                if (!Flags_GetRandomizerInf(requiredInf)) {
                    *canBuy = CANBUY_RESULT_CANT_GET_NOW;
                    *should = true;
                }
            }
            break;
        }
        case VB_ALLOW_ENTRANCE_CS_FOR_EITHER_AGE: {
            s32 entranceIndex = va_arg(args, s32);

            // Allow Nabooru fight cutscene to play for child in rando
            if (entranceIndex == ENTR_SPIRIT_TEMPLE_BOSS_ENTRANCE) {
                *should = true;
            }
            break;
        }
        case VB_PLAY_SLOW_CHEST_CS: {
            // We force fast chests if SkipGetItemAnimation is enabled because the camera in the CS looks pretty wonky
            // otherwise
            if (CVarGetInteger(CVAR_RANDOMIZER_ENHANCEMENT("TimeSavers.SkipGetItemAnimation"), SGIA_JUNK)) {
                *should = false;
            }
            break;
        }
        case VB_GIVE_ITEM_FROM_CHEST: {
            EnBox* chest = va_arg(args, EnBox*);
            RandomizerCheck rc = OTRGlobals::Instance->gRandomizer->GetCheckFromActor(
                chest->dyna.actor.id, gPlayState->sceneNum, chest->dyna.actor.params);
            if (!OTRGlobals::Instance->gRandoContext->IsLocationShuffled(rc)) {
                break;
            }

            // if this is a treasure chest game chest then set the appropriate rando inf
            RandomizerSetChestGameRandomizerInf(rc);

            Player* player = GET_PLAYER(gPlayState);
            Player_SetupWaitForPutAway(gPlayState, player, func_8083A434_override);

            *should = false;
            break;
        }
        case VB_SPAWN_FIRE_ARROW:
            *should = !Flags_GetTreasure(gPlayState, 0x1F);
            break;
        case VB_PLAY_NABOORU_CAPTURED_CS:
            // This behavior is replicated for randomizer in RandomizerOnItemReceiveHandler
            *should = false;
            break;
        case VB_SHIEK_PREPARE_TO_GIVE_SERENADE_OF_WATER: {
            *should =
                !Flags_GetEventChkInf(EVENTCHKINF_LEARNED_SERENADE_OF_WATER) && !Flags_GetTreasure(gPlayState, 0x2);
            break;
        }
        case VB_BE_ELIGIBLE_FOR_SERENADE_OF_WATER:
            *should =
                !Flags_GetEventChkInf(EVENTCHKINF_LEARNED_SERENADE_OF_WATER) && Flags_GetTreasure(gPlayState, 0x2);
            break;
        case VB_BE_ELIGIBLE_FOR_PRELUDE_OF_LIGHT:
            *should =
                !Flags_GetEventChkInf(EVENTCHKINF_LEARNED_PRELUDE_OF_LIGHT) && CHECK_QUEST_ITEM(QUEST_MEDALLION_FOREST);
            break;
        case VB_MIDO_SPAWN:
            if (RAND_GET_OPTION(RSK_FOREST).IsNot(RO_CLOSED_FOREST_OFF) &&
                !Flags_GetEventChkInf(EVENTCHKINF_SHOWED_MIDO_SWORD_SHIELD)) {
                *should = true;
            }
            break;
        case VB_MOVE_MIDO_IN_KOKIRI_FOREST:
            if (RAND_GET_OPTION(RSK_FOREST).Is(RO_CLOSED_FOREST_OFF) && gSaveContext.cutsceneIndex == 0) {
                *should = true;
            }
            break;
        case VB_MALON_RETURN_FROM_CASTLE:
            *should = Flags_GetEventChkInf(EVENTCHKINF_TALON_RETURNED_FROM_CASTLE) &&
                      Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_POCKET_EGG);
            break;
        case VB_SEND_MALON_HOME:
            *should = Flags_GetRandomizerInf(RAND_INF_TALON_SENT_MALON_HOME);
            break;
        case VB_MIDO_CONSIDER_DEKU_TREE_DEAD:
            *should = Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_KOKIRI_EMERALD_DEKU_TREE_DEAD);
            break;
        case VB_OPEN_CHEST:
            *should = *should && Flags_GetRandomizerInf(RAND_INF_CAN_OPEN_CHEST);
            break;
        case VB_OPEN_KOKIRI_FOREST:
            *should = Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_KOKIRI_EMERALD_DEKU_TREE_DEAD) ||
                      RAND_GET_OPTION(RSK_FOREST).IsNot(RO_CLOSED_FOREST_ON);
            break;
        case VB_BE_ELIGIBLE_FOR_DARUNIAS_JOY_REWARD:
            *should = !Flags_GetRandomizerInf(RAND_INF_DARUNIAS_JOY);
            break;
        case VB_BE_ELIGIBLE_FOR_LIGHT_ARROWS:
            *should = LINK_IS_ADULT && (gEntranceTable[gSaveContext.entranceIndex].scene == SCENE_TEMPLE_OF_TIME) &&
                      !Flags_GetEventChkInf(EVENTCHKINF_RETURNED_TO_TEMPLE_OF_TIME_WITH_ALL_MEDALLIONS) &&
                      MeetsLACSRequirements();
            break;
        case VB_BE_ELIGIBLE_FOR_NOCTURNE_OF_SHADOW:
            *should = !Flags_GetEventChkInf(EVENTCHKINF_BONGO_BONGO_ESCAPED_FROM_WELL) && LINK_IS_ADULT &&
                      gEntranceTable[((void)0, gSaveContext.entranceIndex)].scene == SCENE_KAKARIKO_VILLAGE &&
                      CHECK_QUEST_ITEM(QUEST_MEDALLION_FOREST) && CHECK_QUEST_ITEM(QUEST_MEDALLION_FIRE) &&
                      CHECK_QUEST_ITEM(QUEST_MEDALLION_WATER) && gSaveContext.cutsceneIndex < 0xFFF0;
            break;
        case VB_BE_ELIGIBLE_FOR_CHILD_ROLLING_GORON_REWARD: {
            // Don't require a bomb bag to get prize in rando
            *should = true;
            break;
        }
        case VB_BE_ELIGIBLE_FOR_MAGIC_BEANS_PURCHASE: {
            if (RAND_GET_OPTION(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_BEANS_ONLY) ||
                RAND_GET_OPTION(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL)) {
                *should = gSaveContext.rupees >=
                          OTRGlobals::Instance->gRandoContext->GetItemLocation(RC_ZR_MAGIC_BEAN_SALESMAN)->GetPrice();
            } else if (RAND_GET_OPTION(RSK_SKIP_PLANTING_BEANS)) {
                *should = gSaveContext.rupees >= 60;
            } else if (BEANS_BOUGHT == 9) {
                *should = gSaveContext.rupees >= 99;
            }
            break;
        }
        case VB_MAGIC_BEAN_SALESMAN_TAKE_MONEY: {
            if (BEANS_BOUGHT == 9) {
                Rupees_ChangeBy(-99);
                *should = false;
            }
            break;
        }
        case VB_CAN_BRIBE_HEISHI2: {
            EnHeishi2* guard = va_arg(args, EnHeishi2*);
            guard->actor.textId = 0x7072;
            guard->unk_300 = TEXT_STATE_CHOICE;
            guard->unk_30E = 1;
            guard->actionFunc = func_80A5475C;
            *should = false;
            break;
        }
        case VB_GIVE_ITEM_MASTER_SWORD:
            if (RAND_GET_OPTION(RSK_SHUFFLE_MASTER_SWORD) || RAND_GET_OPTION(RSK_STARTING_MASTER_SWORD)) {
                *should = false;
            } else {
                *should = true;
                Rando::Context::GetInstance()->GetItemLocation(RC_TOT_MASTER_SWORD)->SetCheckStatus(RCSHOW_COLLECTED);
                CheckTracker::RecalculateAllAreaTotals();
            }
            break;
        case VB_ITEM00_DESPAWN: {
            EnItem00* item00 = va_arg(args, EnItem00*);
            if (item00->actor.params == ITEM00_HEART_PIECE || item00->actor.params == ITEM00_SMALL_KEY) {
                RandomizerCheck rc = OTRGlobals::Instance->gRandomizer->GetCheckFromActor(
                    item00->actor.id, gPlayState->sceneNum, item00->ogParams);
                if (rc != RC_UNKNOWN_CHECK) {
                    item00->randoInf = RAND_INF_MAX;
                    item00->actor.params = ITEM00_SOH_DUMMY;
                    item00->itemEntry = Rando::Context::GetInstance()->GetFinalGIEntry(
                        rc, true, (GetItemID)Rando::StaticData::GetLocation(rc)->GetVanillaItem());
                    item00->actor.draw = (ActorFunc)EnItem00_DrawRandomizedItem;
                    *should = Rando::Context::GetInstance()->GetItemLocation(rc)->HasObtained();
                }
            } else if (item00->actor.params == ITEM00_SOH_GIVE_ITEM_ENTRY ||
                       item00->actor.params == ITEM00_SOH_GIVE_ITEM_ENTRY_GI) {
                GetItemEntry itemEntry = RandomizerItemQueueCurrentEntry();
                item00->itemEntry = itemEntry;
                item00->actor.draw = (ActorFunc)EnItem00_DrawRandomizedItem;
            }
            break;
        }
        case VB_ITEM_B_HEART_DESPAWN: {
            ItemBHeart* itemBHeart = va_arg(args, ItemBHeart*);
            RandomizerCheck rc = OTRGlobals::Instance->gRandomizer->GetCheckFromActor(
                itemBHeart->actor.id, gPlayState->sceneNum, itemBHeart->actor.params);
            if (rc != RC_UNKNOWN_CHECK) {
                itemBHeart->sohItemEntry = Rando::Context::GetInstance()->GetFinalGIEntry(
                    rc, true, (GetItemID)Rando::StaticData::GetLocation(rc)->GetVanillaItem());
                itemBHeart->actor.draw = (ActorFunc)ItemBHeart_DrawRandomizedItem;
                itemBHeart->actor.update = (ActorFunc)ItemBHeart_UpdateRandomizedItem;
                *should = Rando::Context::GetInstance()->GetItemLocation(rc)->HasObtained();
            }
            break;
        }
        case VB_MALON_ALREADY_TAUGHT_EPONAS_SONG: {
            *should = Flags_GetRandomizerInf(RAND_INF_LEARNED_EPONA_SONG);
            break;
        }
        case VB_KING_ZORA_THANK_CHILD: {
            // Allow turning in Ruto's letter even if you have already rescued her
            if (!Flags_GetEventChkInf(EVENTCHKINF_KING_ZORA_MOVED)) {
                GET_PLAYER(gPlayState)->exchangeItemId = EXCH_ITEM_LETTER_RUTO;
            }
            *should = Flags_GetEventChkInf(EVENTCHKINF_USED_JABU_JABUS_BELLY_BLUE_WARP);
            break;
        }
        case VB_BE_ABLE_TO_EXCHANGE_RUTOS_LETTER: {
            *should = LINK_IS_CHILD;
            break;
        }
        case VB_KING_ZORA_BE_MOVED: {
            *should = false;
            switch (RAND_GET_OPTION(RSK_ZORAS_FOUNTAIN).Get()) {
                case RO_ZF_CLOSED:
                    if (Flags_GetEventChkInf(EVENTCHKINF_KING_ZORA_MOVED)) {
                        *should = true;
                    }
                    break;
                case RO_ZF_CLOSED_CHILD:
                    if (LINK_IS_ADULT) {
                        *should = true;
                    } else if (Flags_GetEventChkInf(EVENTCHKINF_KING_ZORA_MOVED)) {
                        *should = true;
                    }
                    break;
                case RO_ZF_OPEN:
                    *should = true;
                    break;
            }
            break;
        }
        case VB_KING_ZORA_TUNIC_CHECK: {
            if (!Flags_GetRandomizerInf(RAND_INF_KING_ZORA_THAWED)) {
                *should = false;
            }
            break;
        }
        case VB_BIGGORON_CONSIDER_SWORD_COLLECTED: {
            *should = Flags_GetRandomizerInf(RAND_INF_ADULT_TRADES_DMT_TRADE_CLAIM_CHECK);
            break;
        }
        case VB_BIGGORON_CONSIDER_TRADE_COMPLETE: {
            // This being true will prevent other biggoron trades, there are already safeguards in place to prevent
            // claim check from being traded multiple times, so we don't really need the quest to ever be considered
            // "complete"
            *should = false;
            break;
        }
        case VB_PREVENT_STRENGTH: {
            if (!Flags_GetRandomizerInf(RAND_INF_CAN_GRAB)) {
                GET_PLAYER(gPlayState)->stateFlags2 &= ~PLAYER_STATE2_MOVING_DYNAPOLY;
                *should = true;
            }
            break;
        }
        case VB_GORONS_CONSIDER_FIRE_TEMPLE_FINISHED: {
            *should = Flags_GetEventChkInf(EVENTCHKINF_USED_FIRE_TEMPLE_BLUE_WARP);
            break;
        }
        case VB_GORONS_CONSIDER_DODONGOS_CAVERN_FINISHED: {
            *should = Flags_GetEventChkInf(EVENTCHKINF_USED_DODONGOS_CAVERN_BLUE_WARP);
            break;
        }
        case VB_GORONS_CONSIDER_TUNIC_COLLECTED: {
            *should = Flags_GetInfTable(INFTABLE_GORON_CITY_DOORS_UNLOCKED);
            break;
        }
        case VB_GIVE_ITEM_FROM_ITEM_00: {
            EnItem00* item00 = va_arg(args, EnItem00*);
            if (item00->actor.params == ITEM00_SOH_DUMMY) {
                if (item00->randoInf != RAND_INF_MAX) {
                    Flags_SetRandomizerInf(item00->randoInf);
                } else {
                    Flags_SetCollectible(gPlayState, item00->collectibleFlag);
                }
                Actor_Kill(&item00->actor);
                *should = false;
            } else if (item00->actor.params == ITEM00_SOH_GIVE_ITEM_ENTRY) {
                Audio_PlaySoundGeneral(NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                if (item00->itemEntry.modIndex == MOD_NONE) {
                    if (item00->itemEntry.getItemId == GI_SWORD_BGS) {
                        gSaveContext.bgsFlag = true;
                    }
                    Item_Give(gPlayState, static_cast<uint8_t>(item00->itemEntry.itemId));
                } else if (item00->itemEntry.modIndex == MOD_RANDOMIZER) {
                    if (item00->itemEntry.getItemId == RG_ICE_TRAP) {
                        gSaveContext.ship.pendingIceTrapCount++;
                    } else {
                        Randomizer_Item_Give(gPlayState, item00->itemEntry);
                    }
                }

                if (item00->itemEntry.modIndex == MOD_NONE) {
                    std::string message;

                    switch (gSaveContext.language) {
                        case LANGUAGE_FRA:
                            message = "Vous obtenez: ";
                            break;
                        case LANGUAGE_GER:
                            message = "Du erhältst: ";
                            break;
                        case LANGUAGE_ENG:
                        default:
                            message = "You found ";
                            break;
                    }

                    Notification::Emit({
                        .itemIcon = GetTextureForItemId(item00->itemEntry.itemId),
                        .message = message,
                        .suffix = SohUtils::GetItemName(item00->itemEntry.itemId),
                    });
                } else if (item00->itemEntry.modIndex == MOD_RANDOMIZER) {
                    std::string message;
                    std::string itemName;

                    switch (gSaveContext.language) {
                        case LANGUAGE_FRA:
                            message = "Vous obtenez: ";
                            itemName = Rando::StaticData::RetrieveItem((RandomizerGet)item00->itemEntry.getItemId)
                                           .GetName()
                                           .french;
                            break;
                        case LANGUAGE_GER:
                            message = "Du erhältst: ";
                            itemName = Rando::StaticData::RetrieveItem((RandomizerGet)item00->itemEntry.getItemId)
                                           .GetName()
                                           .german;
                            break;
                        case LANGUAGE_ENG:
                        default:
                            message = "You found ";
                            itemName = Rando::StaticData::RetrieveItem((RandomizerGet)item00->itemEntry.getItemId)
                                           .GetName()
                                           .english;
                            break;
                    }

                    Notification::Emit({
                        .message = message,
                        .suffix = itemName,
                    });
                }

                // This is typically called when you close the text box after getting an item, in case a previous
                // function hid the interface.
                gSaveContext.unk_13EA = 0;
                Interface_ChangeAlpha(0x32);
                // EnItem00_SetupAction(item00, func_8001E5C8);
                // *should = false;
            } else if (item00->actor.params == ITEM00_SOH_GIVE_ITEM_ENTRY_GI) {
                if (!Actor_HasParent(&item00->actor, gPlayState)) {
                    GiveItemEntryFromActorWithFixedRange(&item00->actor, gPlayState, item00->itemEntry);
                }
                EnItem00_SetupAction(item00, func_8001E5C8);
                *should = false;
            }
            break;
        }
        case VB_BE_ELIGIBLE_FOR_SARIAS_SONG: {
            *should = !Flags_GetEventChkInf(EVENTCHKINF_LEARNED_SARIAS_SONG);
            break;
        }
        case VB_GIVE_ITEM_FROM_DEKU_THEATER: {
            EnDntJiji* enDntJiji = va_arg(args, EnDntJiji*);
            enDntJiji->actionFunc = EnDntJiji_GivePrize;
            *should = false;
            break;
        }
        case VB_GIVE_ITEM_FROM_GRANNYS_SHOP: {
            if (!EnDs_RandoCanGetGrannyItem()) {
                break;
            }
            EnDs* granny = va_arg(args, EnDs*);
            // Only setting the inf if we've actually gotten the rando item and not the vanilla blue potion
            Flags_SetRandomizerInf(RAND_INF_MERCHANTS_GRANNYS_SHOP);
            granny->actor.parent = NULL;
            granny->actionFunc = EnDs_Talk;
            *should = false;
            break;
        }
        case VB_GIVE_ITEM_FROM_ANJU_AS_CHILD: {
            Flags_SetItemGetInf(ITEMGETINF_0C);
            *should = false;
            break;
        }
        case VB_GIVE_ITEM_FROM_ANJU_AS_ADULT: {
            EnNiwLady* enNiwLady = va_arg(args, EnNiwLady*);
            Flags_SetItemGetInf(ITEMGETINF_2C);
            enNiwLady->actionFunc = func_80ABA778;
            *should = false;
            break;
        }
        case VB_CHECK_RANDO_PRICE_OF_CARPET_SALESMAN: {
            if (EnJs_RandoCanGetCarpetMerchantItem()) {
                *should =
                    gSaveContext.rupees <
                    OTRGlobals::Instance->gRandoContext->GetItemLocation(RC_WASTELAND_BOMBCHU_SALESMAN)->GetPrice();
            }
            break;
        }
        case VB_GIVE_ITEM_FROM_CARPET_SALESMAN: {
            EnJs* enJs = va_arg(args, EnJs*);
            if (EnJs_RandoCanGetCarpetMerchantItem()) {
                Rupees_ChangeBy(
                    OTRGlobals::Instance->gRandoContext->GetItemLocation(RC_WASTELAND_BOMBCHU_SALESMAN)->GetPrice() *
                    -1);
                enJs->actor.parent = NULL;
                enJs->actor.textId = TEXT_CARPET_SALESMAN_ARMS_DEALER;
                enJs->actionFunc = (EnJsActionFunc)func_80A890C0;
                enJs->actor.flags |= ACTOR_FLAG_TALK_OFFER_AUTO_ACCEPTED;
                Flags_SetRandomizerInf(RAND_INF_MERCHANTS_CARPET_SALESMAN);
                *should = true;
            }
            break;
        }
        case VB_GIVE_BOMBCHUS_FROM_CARPET_SALESMAN: {
            *should =
                RAND_GET_OPTION(RSK_BOMBCHU_BAG).Is(RO_BOMBCHU_BAG_NONE) || INV_CONTENT(ITEM_BOMBCHU) == ITEM_BOMBCHU;
            break;
        }
        case VB_CHECK_RANDO_PRICE_OF_MEDIGORON: {
            if (EnGm_RandoCanGetMedigoronItem()) {
                *should = gSaveContext.rupees <
                          OTRGlobals::Instance->gRandoContext->GetItemLocation(RC_GC_MEDIGORON)->GetPrice();
            }
            break;
        }

        case VB_GIVE_ITEM_FROM_MEDIGORON:
        case VB_BE_ELIGIBLE_FOR_GIANTS_KNIFE_PURCHASE: {
            if (EnGm_RandoCanGetMedigoronItem()) {
                if (id == VB_GIVE_ITEM_FROM_MEDIGORON) {
                    EnGm* enGm = va_arg(args, EnGm*);
                    Flags_SetInfTable(INFTABLE_B1);
                    Flags_SetRandomizerInf(RAND_INF_MERCHANTS_MEDIGORON);
                    enGm->actor.parent = NULL;
                    enGm->actionFunc = (EnGmActionFunc)func_80A3DC44;
                    Rupees_ChangeBy(OTRGlobals::Instance->gRandoContext->GetItemLocation(RC_GC_MEDIGORON)->GetPrice() *
                                    -1);
                    *should = false;
                } else {
                    // Resets "Talked to Medigoron" flag in infTable to restore initial conversation state
                    Flags_UnsetInfTable(INFTABLE_B1);
                    *should = true;
                }
            }
            break;
        }
        case VB_GIVE_ITEM_FROM_MAGIC_BEAN_SALESMAN: {
            EnMs* enMs = va_arg(args, EnMs*);
            if (RAND_GET_OPTION(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_BEANS_ONLY) ||
                RAND_GET_OPTION(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL)) {
                Rupees_ChangeBy(
                    OTRGlobals::Instance->gRandoContext->GetItemLocation(RC_ZR_MAGIC_BEAN_SALESMAN)->GetPrice() * -1);
                BEANS_BOUGHT = 10;
                // Only set inf for buying rando check
                Flags_SetRandomizerInf(RAND_INF_MERCHANTS_MAGIC_BEAN_SALESMAN);
                enMs->actionFunc = (EnMsActionFunc)EnMs_Wait;
                *should = false;
            } else if (RAND_GET_OPTION(RSK_SKIP_PLANTING_BEANS)) {
                Rupees_ChangeBy(-60);
                Item_Give(NULL, ITEM_BEAN);
                BEANS_BOUGHT = 10;
                AMMO(ITEM_BEAN) = 0;
                gSaveContext.sceneFlags[SCENE_DEATH_MOUNTAIN_CRATER].swch |= (1 << 3);
                gSaveContext.sceneFlags[SCENE_DEATH_MOUNTAIN_TRAIL].swch |= (1 << 6);
                gSaveContext.sceneFlags[SCENE_DESERT_COLOSSUS].swch |= (1 << 24);
                gSaveContext.sceneFlags[SCENE_GERUDO_VALLEY].swch |= (1 << 3);
                gSaveContext.sceneFlags[SCENE_GRAVEYARD].swch |= (1 << 3);
                gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].swch |= (1 << 9);
                gSaveContext.sceneFlags[SCENE_LAKE_HYLIA].swch |= (1 << 1);
                gSaveContext.sceneFlags[SCENE_LOST_WOODS].swch |= (1 << 4) | (1 << 18);
                gSaveContext.sceneFlags[SCENE_ZORAS_RIVER].swch |= (1 << 3);
                ObjBean* bean = (ObjBean*)Actor_Find(&gPlayState->actorCtx, ACTOR_OBJ_BEAN, ACTORCAT_BG);
                if (bean != nullptr) {
                    Flags_SetSwitch(gPlayState, bean->dyna.actor.params & 0x3F);
                    func_80B8FE00(bean);
                }
                enMs->actionFunc = (EnMsActionFunc)EnMs_Wait;
                *should = false;
            }
            break;
        }
        case VB_DEKU_THEATER_FINISH_GIVING_PRIZE:
            *should = true;
            break;
        case VB_FROGS_GO_TO_IDLE: {
            EnFr* enFr = va_arg(args, EnFr*);

            if ((enFr->songIndex >= FROG_STORMS && enFr->reward == GI_HEART_PIECE) ||
                (enFr->songIndex < FROG_STORMS && enFr->reward == GI_RUPEE_PURPLE)) {
                *should = true;
            }
            break;
        }
        case VB_TEMP_B_TREAT_AS_OCCUPIED:
            // Treat a swordless player's empty B as occupied so they enter the temp-B force path.
            *should = *should || RandoCanTrackSwordless(va_arg(args, PlayState*));
            break;
        case VB_TEMP_B_STASH_SWORDLESS:
            // Relocate the just-stashed temp-B to the swordless sentinel for later restoration.
            if (RandoCanTrackSwordless(va_arg(args, PlayState*))) {
                gSaveContext.buttonStatus[0] = SWORDLESS_STATUS;
            }
            break;
        case VB_TEMP_B_SHOULD_RESTORE:
            // Also restore the B button when a swordless sentinel was stashed.
            *should = *should || gSaveContext.buttonStatus[0] == SWORDLESS_STATUS;
            break;
        case VB_TEMP_B_RESTORE_SWORDLESS:
            // Convert the swordless sentinel back into an empty (swordless) B button.
            if (gSaveContext.buttonStatus[0] == SWORDLESS_STATUS) {
                gSaveContext.equips.buttonItems[0] = ITEM_NONE;
                gSaveContext.buttonStatus[0] = BTN_ENABLED;
            }
            break;
        case VB_TRADE_POCKET_CUCCO: {
            EnNiwLady* enNiwLady = va_arg(args, EnNiwLady*);
            Flags_UnsetRandomizerInf(RAND_INF_ADULT_TRADES_HAS_POCKET_CUCCO);
            Inventory_ReplaceItem(gPlayState, ITEM_POCKET_CUCCO, Randomizer_GetNextAdultTradeItem());
            // Trigger the reward now
            Flags_SetItemGetInf(ITEMGETINF_2E);
            enNiwLady->actionFunc = func_80ABA778;

            *should = false;
            break;
        }
        case VB_TRADE_COJIRO: {
            Flags_UnsetRandomizerInf(RAND_INF_ADULT_TRADES_HAS_COJIRO);
            Inventory_ReplaceItem(gPlayState, ITEM_COJIRO, Randomizer_GetNextAdultTradeItem());
            *should = false;
            break;
        }
        case VB_TRADE_ODD_MUSHROOM: {
            EnDs* granny = va_arg(args, EnDs*);
            Flags_UnsetRandomizerInf(RAND_INF_ADULT_TRADES_HAS_ODD_MUSHROOM);
            Inventory_ReplaceItem(gPlayState, ITEM_ODD_MUSHROOM, Randomizer_GetNextAdultTradeItem());
            // Trigger the reward now
            Flags_SetItemGetInf(ITEMGETINF_30);
            granny->actor.textId = 0x504F;
            granny->actionFunc = (EnDsActionFunc)EnDs_TalkAfterGiveOddPotion;
            granny->actor.flags &= ~ACTOR_FLAG_TALK;
            *should = false;
            break;
        }
        case VB_TRADE_ODD_POTION: {
            EnKo* enKo = va_arg(args, EnKo*);
            Flags_UnsetRandomizerInf(RAND_INF_ADULT_TRADES_HAS_ODD_POTION);
            Inventory_ReplaceItem(gPlayState, ITEM_ODD_POTION, Randomizer_GetNextAdultTradeItem());
            // Trigger the reward now
            Flags_SetItemGetInf(ITEMGETINF_31);
            *should = false;
            break;
        }
        case VB_TRADE_SAW: {
            Flags_UnsetRandomizerInf(RAND_INF_ADULT_TRADES_HAS_SAW);
            Inventory_ReplaceItem(gPlayState, ITEM_SAW, Randomizer_GetNextAdultTradeItem());
            *should = false;
            break;
        }
        case VB_ADULT_KING_ZORA_ITEM_GIVE: {
            EnKz* enKz = va_arg(args, EnKz*);
            Input input = gPlayState->state.input[0];

            if (CVarGetInteger(CVAR_ENHANCEMENT("EarlyEyeballFrog"), 0)) {
                // For early eyeball frog hook override, simulate collection delay behavior by just checking for the R
                // button being held while wearing a shield, and a trade item lower than frog in inventory
                bool hasShieldHoldingR = (CHECK_BTN_ANY(input.cur.button, BTN_R) &&
                                          CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) > EQUIP_VALUE_SHIELD_NONE);

                if (func_8002F368(gPlayState) == EXCH_ITEM_PRESCRIPTION ||
                    (hasShieldHoldingR && INV_CONTENT(ITEM_TRADE_ADULT) < ITEM_FROG)) {
                    Flags_SetRandomizerInf(RAND_INF_ADULT_TRADES_ZD_TRADE_PRESCRIPTION);
                    Flags_UnsetRandomizerInf(RAND_INF_ADULT_TRADES_HAS_PRESCRIPTION);
                    Inventory_ReplaceItem(gPlayState, ITEM_PRESCRIPTION, Randomizer_GetNextAdultTradeItem());
                } else {
                    Flags_SetRandomizerInf(RAND_INF_KING_ZORA_THAWED);
                }
            } else {
                if (enKz->isTrading) {
                    Flags_SetRandomizerInf(RAND_INF_ADULT_TRADES_ZD_TRADE_PRESCRIPTION);
                    Flags_UnsetRandomizerInf(RAND_INF_ADULT_TRADES_HAS_PRESCRIPTION);
                    Inventory_ReplaceItem(gPlayState, ITEM_PRESCRIPTION, Randomizer_GetNextAdultTradeItem());
                } else {
                    Flags_SetRandomizerInf(RAND_INF_KING_ZORA_THAWED);
                }
            }
            *should = false;
            break;
        }
        case VB_TRADE_FROG: {
            Flags_UnsetRandomizerInf(RAND_INF_ADULT_TRADES_HAS_FROG);
            Inventory_ReplaceItem(gPlayState, ITEM_FROG, Randomizer_GetNextAdultTradeItem());
            *should = false;
            break;
        }
        case VB_BUSINESS_SCRUB_DESPAWN: {
            EnShopnuts* enShopnuts = va_arg(args, EnShopnuts*);
            s16 respawnData = gSaveContext.respawn[RESPAWN_MODE_RETURN].data & ((1 << 8) - 1);
            ScrubIdentity scrubIdentity = IdentifyScrub(gPlayState->sceneNum, enShopnuts->actor.params, respawnData);

            if (scrubIdentity.identity.randomizerCheck != RC_UNKNOWN_CHECK) {
                *should = Flags_GetRandomizerInf(scrubIdentity.identity.randomizerInf);
            }
            break;
        }
        case VB_GIVE_ITEM_FROM_BUSINESS_SCRUB: {
            EnDns* enDns = va_arg(args, EnDns*);
            *should = !ObjectExtension::GetInstance().Has<ScrubIdentity>(enDns);
            break;
        }
        // To explain the logic because Fado and Grog are linked:
        // - If you have Cojiro, then spawn Grog and not Fado.
        // - If you don't have Cojiro but do have Odd Potion, spawn Fado and not Grog.
        // - If you don't have either, spawn Grog if you haven't traded the Odd Mushroom.
        // - If you don't have either but have traded the mushroom, don't spawn either.
        case VB_DESPAWN_GROG: {
            if (!RAND_GET_OPTION(RSK_SHUFFLE_ADULT_TRADE)) {
                break;
            }
            if (Flags_GetRandomizerInf(RAND_INF_ADULT_TRADES_HAS_COJIRO)) {
                *should = false;
            } else if (Flags_GetRandomizerInf(RAND_INF_ADULT_TRADES_HAS_ODD_POTION)) {
                *should = true;
            } else {
                *should = Flags_GetItemGetInf(ITEMGETINF_30); // Traded odd mushroom
            }
            break;
        }
        case VB_SPAWN_LW_FADO: {
            if (!RAND_GET_OPTION(RSK_SHUFFLE_ADULT_TRADE)) {
                break;
            }

            if (Flags_GetRandomizerInf(RAND_INF_ADULT_TRADES_HAS_COJIRO)) {
                *should = false;
            } else {
                *should = Flags_GetRandomizerInf(RAND_INF_ADULT_TRADES_HAS_ODD_POTION);
            }

            break;
        }
        case VB_USE_EYEDROP_DIALOGUE: {
            // Skip eye drop text on rando if Link went in the water, so you can still receive the dive check
            EnMk* enMk = va_arg(args, EnMk*);
            *should &= enMk->swimFlag == 0;
            break;
        }
        case VB_OFFER_BLUE_POTION: {
            *should |= RAND_GET_OPTION(RSK_SHUFFLE_ADULT_TRADE).Is(RO_GENERIC_OFF) &&
                       INV_CONTENT(ITEM_CLAIM_CHECK) == ITEM_CLAIM_CHECK;
            break;
        }
        case VB_OKARINA_TAG_COMPLETE: {
            if (gPlayState->sceneNum == SCENE_BOTTOM_OF_THE_WELL) {
                auto dungeon =
                    OTRGlobals::Instance->gRandoContext->GetDungeons()->GetDungeonFromScene(SCENE_BOTTOM_OF_THE_WELL);
                if (dungeon->IsVanilla()) {
                    EnOkarinaTag* enOkarinaTag = va_arg(args, EnOkarinaTag*);
                    if (enOkarinaTag->switchFlag >= 0 && Flags_GetSwitch(gPlayState, enOkarinaTag->switchFlag)) {
                        Flags_UnsetSwitch(gPlayState, enOkarinaTag->switchFlag);
                        *should = false;
                    }
                }
            }
            break;
        }
        case VB_OKARINA_TAG_COMPLETED: {
            if (gPlayState->sceneNum == SCENE_BOTTOM_OF_THE_WELL) {
                auto dungeon =
                    OTRGlobals::Instance->gRandoContext->GetDungeons()->GetDungeonFromScene(SCENE_BOTTOM_OF_THE_WELL);
                if (dungeon->IsVanilla()) {
                    *should = false;
                }
            }
            break;
        }
        case VB_GRANNY_SAY_INSUFFICIENT_RUPEES: {
            if (EnDs_RandoCanGetGrannyItem()) {
                *should = gSaveContext.rupees <
                          OTRGlobals::Instance->gRandoContext->GetItemLocation(RC_KAK_GRANNYS_SHOP)->GetPrice();
            }
            break;
        }
        case VB_GRANNY_TAKE_MONEY: {
            if (EnDs_RandoCanGetGrannyItem()) {
                *should = false;
                Rupees_ChangeBy(OTRGlobals::Instance->gRandoContext->GetItemLocation(RC_KAK_GRANNYS_SHOP)->GetPrice() *
                                -1);
            }
            break;
        }
        case VB_NEED_BOTTLE_FOR_GRANNYS_ITEM: {
            // Allow buying the rando item regardless of having a bottle
            *should &= !EnDs_RandoCanGetGrannyItem();
            break;
        }
        case VB_GIVE_ITEM_FROM_SHOOTING_GALLERY: {
            EnSyatekiMan* enSyatekiMan = va_arg(args, EnSyatekiMan*);
            enSyatekiMan->getItemId = GI_RUPEE_PURPLE;
            if (LINK_IS_ADULT) {
                // Give purple rupee if we've already obtained the reward OR we don't have a bow
                *should = Flags_GetItemGetInf(ITEMGETINF_0E) || CUR_UPG_VALUE(UPG_QUIVER) == 0;
            } else {
                // Give purple rupee if we've already obtained the reward
                *should = Flags_GetItemGetInf(ITEMGETINF_0D);
            }
            break;
        }
        case VB_BE_ELIGIBLE_FOR_ADULT_SHOOTING_GAME_REWARD: {
            *should = CUR_UPG_VALUE(UPG_QUIVER) > 0;
            if (!*should) {
                // In Rando without a quiver, display a message reminding the player to come back with a bow
                Message_StartTextbox(gPlayState, TEXT_SHOOTING_GALLERY_MAN_COME_BACK_WITH_BOW, NULL);
            }
            break;
        }
        case VB_BE_ELIGIBLE_TO_OPEN_DOT: {
            bool eligible =
                RAND_GET_OPTION(RSK_DOOR_OF_TIME).IsNot(RO_DOOROFTIME_CLOSED) ||
                (INV_CONTENT(ITEM_OCARINA_FAIRY) == ITEM_OCARINA_TIME && CHECK_QUEST_ITEM(QUEST_KOKIRI_EMERALD) &&
                 CHECK_QUEST_ITEM(QUEST_GORON_RUBY) && CHECK_QUEST_ITEM(QUEST_ZORA_SAPPHIRE));
            *should = eligible;
            break;
        }
        case VB_GIVE_ITEM_FROM_HORSEBACK_ARCHERY: {
            EnGe1* enGe1 = va_arg(args, EnGe1*);
            // give both rewards at the same time
            if (gSaveContext.minigameScore >= 1000) {
                Flags_SetInfTable(INFTABLE_190);
            }
            if (gSaveContext.minigameScore >= 1500) {
                Flags_SetItemGetInf(ITEMGETINF_0F);
            }
            // move gerudo actor onto her wait loop
            enGe1->actionFunc = EnGe1_Wait_Archery;
            EnGe1_SetAnimationIdle(enGe1);
            // skip the vanilla gives.
            *should = false;
            break;
        }
        case VB_GIVE_ITEM_FROM_SKULLTULA_REWARD: {
            // In z_en_sth.c the rewards are stored in sGetItemIds, the first entry
            // in that array is GI_RUPEE_GOLD, and the reward is picked in EnSth_GivePlayerItem
            // via sGetItemIds[this->actor.params]. This means if actor.params == 0 we're looking
            // at the 100 GS reward
            EnSth* enSth = va_arg(args, EnSth*);
            if (enSth->actor.params == 0) {
                // if nothing is shuffled onto 100 GS,
                // or we already got the 100 GS reward,
                // let the player farm
                if (!RAND_GET_OPTION(RSK_SHUFFLE_100_GS_REWARD) ||
                    Flags_GetRandomizerInf(RAND_INF_KAK_100_GOLD_SKULLTULA_REWARD)) {
                    *should = true;
                    break;
                }

                // we're giving the 100 GS rando reward! set the rando inf
                Flags_SetRandomizerInf(RAND_INF_KAK_100_GOLD_SKULLTULA_REWARD);

                // also set the actionfunc so this doesn't immediately get
                // called again (and lead to a vanilla+rando item give
                // because the flag check will pass next time)
                enSth->actionFunc = (EnSthActionFunc)EnSth_RewardObtainedTalk;
            }
            *should = false;
            break;
        }
        case VB_GIVE_ITEM_FROM_OCARINA_MEMORY_GAME: {
            EnSkj* enSkj = va_arg(args, EnSkj*);
            Flags_SetItemGetInf(ITEMGETINF_17);
            enSkj->actionFunc = (EnSkjActionFunc)EnSkj_CleanupOcarinaGame;
            *should = false;
            break;
        }
        case VB_GIVE_ITEM_FROM_LOST_DOG: {
            EnHy* enHy = va_arg(args, EnHy*);
            Flags_SetInfTable(INFTABLE_191);
            gSaveContext.dogParams = 0;
            gSaveContext.dogIsLost = false;
            enHy->actionFunc = EnHy_Fidget;
            *should = false;
            break;
        }
        case VB_GIVE_ITEM_FROM_BOMBCHU_BOWLING: {
            EnBomBowlPit* enBomBowlPit = va_arg(args, EnBomBowlPit*);
            if (enBomBowlPit->prizeIndex == EXITEM_BOMB_BAG_BOWLING ||
                enBomBowlPit->prizeIndex == EXITEM_HEART_PIECE_BOWLING) {
                *should = false;
            }
            break;
        }
        case VB_GERUDO_GUARD_SET_ACTION_AFTER_TALK:
            if (gPlayState->msgCtx.choiceIndex == 0 && gPlayState->sceneNum == SCENE_GERUDOS_FORTRESS) {
                EnGe2* enGe2 = va_arg(args, EnGe2*);
                EnGe2_SetupCapturePlayer(enGe2, gPlayState);
                *should = false;
            }
            break;
        case VB_GERUDOS_BE_FRIENDLY: {
            *should = CHECK_QUEST_ITEM(QUEST_GERUDO_CARD);
            break;
        }
        case VB_GTG_GATE_BE_OPEN: {
            if (Flags_GetRandomizerInf(RAND_INF_GF_GTG_GATE_PERMANENTLY_OPEN)) {
                *should = true;
            }
            break;
        }
        case VB_GIVE_ITEM_GERUDO_MEMBERSHIP_CARD: {
            Flags_SetRandomizerInf(RAND_INF_TH_ITEM_FROM_LEADER_OF_FORTRESS);
            *should = false;
            break;
        }
        case VB_BE_ELIGIBLE_FOR_RAINBOW_BRIDGE: {
            *should = MeetsRainbowBridgeRequirements();
            break;
        }
        case VB_PLAY_BLUE_WARP_CS: {
            // We need to override just these two temples because they check medallions instead of flags
            if (gPlayState->sceneNum == SCENE_SPIRIT_TEMPLE_BOSS) {
                *should = !Flags_GetRandomizerInf(RAND_INF_DUNGEONS_DONE_SPIRIT_TEMPLE);
            } else if (gPlayState->sceneNum == SCENE_SHADOW_TEMPLE_BOSS) {
                *should = !Flags_GetRandomizerInf(RAND_INF_DUNGEONS_DONE_SHADOW_TEMPLE);
            }
            break;
        }
        case VB_DRAW_AMMO_COUNT: {
            s16 item = *va_arg(args, s16*);
            // don't draw ammo count if you have the infinite upgrade
            if ((item == ITEM_NUT && Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_NUT_UPGRADE)) ||
                (item == ITEM_STICK && Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_STICK_UPGRADE)) ||
                (item == ITEM_BOMB && Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_BOMB_BAG)) ||
                ((item == ITEM_BOW || item == ITEM_BOW_ARROW_FIRE || item == ITEM_BOW_ARROW_ICE ||
                  item == ITEM_BOW_ARROW_LIGHT) &&
                 Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_QUIVER) && gPlayState->shootingGalleryStatus < 2 &&
                 gSaveContext.minigameState != 1) ||
                (item == ITEM_SLINGSHOT && Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_BULLET_BAG) &&
                 gPlayState->shootingGalleryStatus < 2) ||
                (item == ITEM_BOMBCHU && Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_BOMBCHUS) &&
                 gPlayState->bombchuBowlingStatus < 1)) {
                *should = false;
            }
            break;
        }
        case VB_HAVE_OCARINA_NOTE_A4: {
            if (!Flags_GetRandomizerInf(RAND_INF_HAS_OCARINA_C_RIGHT)) {
                *should = false;
            }
            break;
        }
        case VB_HAVE_OCARINA_NOTE_B4: {
            if (!Flags_GetRandomizerInf(RAND_INF_HAS_OCARINA_C_LEFT)) {
                *should = false;
            }
            break;
        }
        case VB_HAVE_OCARINA_NOTE_D4: {
            if (!Flags_GetRandomizerInf(RAND_INF_HAS_OCARINA_A)) {
                *should = false;
            }
            break;
        }
        case VB_HAVE_OCARINA_NOTE_D5: {
            if (!Flags_GetRandomizerInf(RAND_INF_HAS_OCARINA_C_UP)) {
                *should = false;
            }
            break;
        }
        case VB_HAVE_OCARINA_NOTE_F4: {
            if (!Flags_GetRandomizerInf(RAND_INF_HAS_OCARINA_C_DOWN)) {
                *should = false;
            }
            break;
        }
        case VB_SKIP_SCARECROWS_SONG: {
            int ocarinaButtonCount = 0;
            for (int i = VB_HAVE_OCARINA_NOTE_A4; i <= VB_HAVE_OCARINA_NOTE_F4; i++) {
                if (GameInteractor_Should((GIVanillaBehavior)i, true)) {
                    ocarinaButtonCount++;
                }
            }

            if (ocarinaButtonCount < 2) {
                *should = false;
                break;
            }

            if (gPlayState->msgCtx.msgMode == MSGMODE_OCARINA_PLAYING && RAND_GET_OPTION(RSK_SKIP_SCARECROWS_SONG)) {
                *should = true;
                break;
            }
            break;
        }
        case VB_RENDER_RUPEE_COUNTER: {
            if (!Flags_GetRandomizerInf(RAND_INF_HAS_WALLET) || Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_MONEY)) {
                *should = false;
            }
            break;
        }
        case VB_BE_ABLE_TO_PLAY_BOMBCHU_BOWLING: {
            // Only check for bomb bag when bombchus aren't in logic
            // and only check for bombchus when bombchus are in logic
            *should = INV_CONTENT((RAND_GET_OPTION(RSK_BOMBCHU_BAG) ? ITEM_BOMBCHU : ITEM_BOMB)) != ITEM_NONE;
            break;
        }
        case VB_SHOULD_CHECK_FOR_FISHING_RECORD: {
            f32 sFishOnHandLength = *va_arg(args, f32*);
            *should = *should || ShouldGiveFishingPrize(sFishOnHandLength);
            break;
        }
        case VB_SHOULD_SET_FISHING_RECORD: {
            VBFishingData* fishData = va_arg(args, VBFishingData*);
            *should = (s16)fishData->sFishingRecordLength < (s16)fishData->fishWeight;
            if (!*should) {
                *fishData->sFishOnHandLength = 0.0f;
            }
            break;
        }
        case VB_SHOULD_GIVE_VANILLA_FISHING_PRIZE: {
            VBFishingData* fishData = va_arg(args, VBFishingData*);
            *should = !IS_RANDO && ShouldGiveFishingPrize(fishData->fishWeight);
            break;
        }
        case VB_GIVE_RANDO_FISHING_PRIZE: {
            if (IS_RANDO) {
                VBFishingData* fishData = va_arg(args, VBFishingData*);
                if (*fishData->sFishOnHandIsLoach) {
                    if (!Flags_GetRandomizerInf(RAND_INF_CAUGHT_LOACH) &&
                        OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_FISHSANITY) ==
                            RO_FISHSANITY_HYRULE_LOACH) {
                        Flags_SetRandomizerInf(RAND_INF_CAUGHT_LOACH);
                        Message_StartTextbox(gPlayState, TEXT_FISHING_RELEASE_THIS_ONE, NULL);
                        *should = true;
                        fishData->actor->stateAndTimer = 20;
                    }
                } else {
                    if (ShouldGiveFishingPrize(fishData->fishWeight)) {
                        if (LINK_IS_CHILD) {
                            Flags_SetRandomizerInf(RAND_INF_CHILD_FISHING);
                            HIGH_SCORE(HS_FISHING) |= HS_FISH_PRIZE_CHILD;
                        } else {
                            Flags_SetRandomizerInf(RAND_INF_ADULT_FISHING);
                            HIGH_SCORE(HS_FISHING) |= HS_FISH_PRIZE_ADULT;
                        }
                        *should = true;
                        *fishData->sSinkingLureLocation = (u8)Rand_ZeroFloat(3.999f) + 1;
                        fishData->actor->stateAndTimer = 0;
                    }
                }
            }
            break;
        }
        case VB_GIVE_RANDO_GLITCH_FISHING_PRIZE: {
            if (IS_RANDO) {
                Fishing* fishing = va_arg(args, Fishing*);
                if (!Flags_GetRandomizerInf(RAND_INF_ADULT_FISHING)) {
                    Flags_SetRandomizerInf(RAND_INF_ADULT_FISHING);
                }
                *should = true;
                fishing->stateAndTimer = 0;
            }
            break;
        }
        case VB_TRADE_TIMER_EYEDROPS: {
            EnMk* enMk = va_arg(args, EnMk*);
            Flags_SetRandomizerInf(RAND_INF_ADULT_TRADES_LH_TRADE_FROG);
            enMk->actor.flags &= ~ACTOR_FLAG_TALK_OFFER_AUTO_ACCEPTED;
            enMk->actionFunc = EnMk_Wait;
            enMk->flags |= 1;
            *should = false;
            break;
        }
        // We need to override the vanilla behavior here because the player might sequence break and get Ruto kidnapped
        // before accessing other checks that require Ruto. So if she's kidnapped we allow her to spawn again
        case VB_RUTO_BE_CONSIDERED_NOT_KIDNAPPED: {
            *should = !Flags_GetInfTable(INFTABLE_145) || Flags_GetInfTable(INFTABLE_146);
            break;
        }
        case VB_SET_VOIDOUT_FROM_SURFACE: {
            // ENTRTODO: Move all entrance rando handling to a dedicated file
            std::vector<s16> entrPersistTempFlags = {
                ENTR_DEKU_TREE_BOSS_ENTRANCE,     ENTR_DEKU_TREE_BOSS_DOOR,        ENTR_DODONGOS_CAVERN_BOSS_ENTRANCE,
                ENTR_DODONGOS_CAVERN_BOSS_DOOR,   ENTR_JABU_JABU_BOSS_ENTRANCE,    ENTR_JABU_JABU_BOSS_DOOR,
                ENTR_FOREST_TEMPLE_BOSS_ENTRANCE, ENTR_FOREST_TEMPLE_BOSS_DOOR,    ENTR_FIRE_TEMPLE_BOSS_ENTRANCE,
                ENTR_FIRE_TEMPLE_BOSS_DOOR,       ENTR_WATER_TEMPLE_BOSS_ENTRANCE, ENTR_WATER_TEMPLE_BOSS_DOOR,
                ENTR_SPIRIT_TEMPLE_BOSS_ENTRANCE, ENTR_SPIRIT_TEMPLE_BOSS_DOOR,    ENTR_SHADOW_TEMPLE_BOSS_ENTRANCE,
                ENTR_SHADOW_TEMPLE_BOSS_DOOR,     ENTR_SPIRIT_TEMPLE_ENTRANCE,
            };

            s16 originalEntrance = (s16)va_arg(args, int);

            // In Entrance rando, if our respawnFlag is set for a grotto return, we don't want the void out to happen
            if (*should == true && RAND_GET_OPTION(RSK_SHUFFLE_ENTRANCES)) {
                // Check for dungeon special entrances that are randomized to a new location
                if (std::find(entrPersistTempFlags.begin(), entrPersistTempFlags.end(), originalEntrance) !=
                        entrPersistTempFlags.end() &&
                    originalEntrance != gPlayState->nextEntranceIndex) {
                    // Normally dungeons use a special voidout between scenes so that entering/exiting a boss room,
                    // or leaving via Spirit Hands and going back in persist temp flags across scenes.
                    // For ER, the temp flags should be wiped out so that they aren't transferred to the new location.
                    gPlayState->actorCtx.flags.tempSwch = 0;
                    gPlayState->actorCtx.flags.tempCollect = 0;

                    // If the respawnFlag is set for a grotto return, we don't want the void out to happen.
                    // Set the data flag to one to prevent the respawn point from being overridden by dungeon doors.
                    if (gSaveContext.respawnFlag == 2) {
                        gSaveContext.respawn[RESPAWN_MODE_DOWN].data = 1;
                        *should = false;
                    }
                }
            }
            break;
        }
        case VB_HEALTH_METER_BE_CRITICAL: {
            if (gSaveContext.health == gSaveContext.healthCapacity) {
                *should = false;
            }
            break;
        }
        case VB_HEISHI2_ACCEPT_ITEM_AS_ZELDAS_LETTER: {
            if (*should) {
                // remove zelda's letter as this is the only use for it
                Flags_UnsetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_LETTER_ZELDA);
            }
            break;
        }
        case VB_FREEZE_ON_SKULL_TOKEN:
        case VB_TRADE_TIMER_ODD_MUSHROOM:
        case VB_TRADE_TIMER_FROG:
        case VB_GIVE_ITEM_FROM_TARGET_IN_WOODS:
        case VB_GIVE_ITEM_FROM_TALONS_CHICKENS:
        case VB_GIVE_ITEM_FROM_DIVING_MINIGAME:
        case VB_GIVE_ITEM_FROM_GORON:
        case VB_GIVE_ITEM_FROM_LAB_DIVE:
        case VB_GIVE_ITEM_FROM_SKULL_KID_SARIAS_SONG:
        case VB_GIVE_ITEM_FROM_MAN_ON_ROOF:
        case VB_GIVE_ITEM_FROM_BLUE_WARP:
        case VB_GIVE_ITEM_FAIRY_OCARINA:
        case VB_GIVE_ITEM_WEIRD_EGG:
        case VB_GIVE_ITEM_LIGHT_ARROW:
        case VB_GIVE_ITEM_STRENGTH_1:
        case VB_GIVE_ITEM_ZELDAS_LETTER:
        case VB_GIVE_ITEM_OCARINA_OF_TIME:
        case VB_GIVE_ITEM_LIGHT_MEDALLION:
        case VB_GIVE_ITEM_FOREST_MEDALLION:
        case VB_GIVE_ITEM_FIRE_MEDALLION:
        case VB_GIVE_ITEM_WATER_MEDALLION:
        case VB_GIVE_ITEM_SPIRIT_MEDALLION:
        case VB_GIVE_ITEM_SHADOW_MEDALLION:
        case VB_CHEST_USE_ICE_EFFECT:
            *should = false;
            break;
        case VB_GIVE_ITEM_SKULL_TOKEN:
            *should = (Rando::Context::GetInstance()->GetOption(RSK_SHUFFLE_TOKENS).Is(RO_TOKENSANITY_OFF));
            break;
        default:
            break;
    }

    va_end(args);
}
