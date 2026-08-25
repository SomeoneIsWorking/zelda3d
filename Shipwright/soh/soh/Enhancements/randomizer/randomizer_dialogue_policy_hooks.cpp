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
#include "randomizer_dialogue_policy_hooks.h"

void RandomizerOnDialogMessageHandler() {
    MessageContext* msgCtx = &gPlayState->msgCtx;
    Actor* actor = msgCtx->talkActor;
    auto ctx = Rando::Context::GetInstance();
    bool revealMerchant = ctx->GetOption(RSK_MERCHANT_TEXT_HINT).Get() != RO_GENERIC_OFF;
    bool nonBeanMerchants = ctx->GetOption(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL_BUT_BEANS) ||
                            ctx->GetOption(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL);

    RandomizerCheck reveal = RC_UNKNOWN_CHECK;
    if (ctx->GetOption(RSK_CHICKENS_HINT) &&
        (msgCtx->textId >= TEXT_ANJU_PLEASE_BRING_MY_CUCCOS_BACK && msgCtx->textId <= TEXT_ANJU_PLEASE_BRING_1_CUCCO)) {
        reveal = RC_KAK_ANJU_AS_CHILD;
    } else {
        switch (msgCtx->textId) {
            case TEXT_SKULLTULA_PEOPLE_IM_CURSED:
                if (actor->params == 1 && ctx->GetOption(RSK_KAK_10_SKULLS_HINT)) {
                    reveal = RC_KAK_10_GOLD_SKULLTULA_REWARD;
                } else if (actor->params == 2 && ctx->GetOption(RSK_KAK_20_SKULLS_HINT)) {
                    reveal = RC_KAK_20_GOLD_SKULLTULA_REWARD;
                } else if (actor->params == 3 && ctx->GetOption(RSK_KAK_30_SKULLS_HINT)) {
                    reveal = RC_KAK_30_GOLD_SKULLTULA_REWARD;
                } else if (actor->params == 4 && ctx->GetOption(RSK_KAK_40_SKULLS_HINT)) {
                    reveal = RC_KAK_40_GOLD_SKULLTULA_REWARD;
                } else if (ctx->GetOption(RSK_KAK_50_SKULLS_HINT)) {
                    reveal = RC_KAK_50_GOLD_SKULLTULA_REWARD;
                }
                break;
            case TEXT_SKULLTULA_PEOPLE_MAKE_YOU_VERY_RICH:
                if (ctx->GetOption(RSK_KAK_100_SKULLS_HINT)) {
                    reveal = RC_KAK_100_GOLD_SKULLTULA_REWARD;
                }
                break;
            case TEXT_MASK_SHOP_SIGN:
                if (ctx->GetOption(RSK_MASK_SHOP_HINT)) {
                    auto itemSkull_loc = ctx->GetItemLocation(RC_DEKU_THEATER_SKULL_MASK);
                    if (itemSkull_loc->GetCheckStatus() == RCSHOW_UNCHECKED) {
                        itemSkull_loc->SetCheckStatus(RCSHOW_IDENTIFIED);
                    }
                    reveal = RC_DEKU_THEATER_MASK_OF_TRUTH;
                }
                break;
            case TEXT_GHOST_SHOP_EXPLAINATION:
            case TEXT_GHOST_SHOP_CARD_HAS_POINTS:
                if (ctx->GetOption(RSK_BIG_POES_HINT)) {
                    reveal = RC_MARKET_10_BIG_POES;
                }
                break;
            case TEXT_MALON_EVERYONE_TURNING_EVIL:
            case TEXT_MALON_I_SING_THIS_SONG:
            case TEXT_MALON_HOW_IS_EPONA_DOING:
            case TEXT_MALON_OBSTICLE_COURSE:
            case TEXT_MALON_INGO_MUST_HAVE_BEEN_TEMPTED:
                if (ctx->GetOption(RSK_MALON_HINT)) {
                    reveal = RC_KF_LINKS_HOUSE_COW;
                }
                break;
            case TEXT_FROGS_UNDERWATER:
                if (ctx->GetOption(RSK_FROGS_HINT)) {
                    reveal = RC_ZR_FROGS_OCARINA_GAME;
                }
                break;
            case TEXT_GF_HBA_SIGN:
            case TEXT_HBA_NOT_ON_HORSE:
            case TEXT_HBA_INITIAL_EXPLAINATION:
            case TEXT_HBA_ALREADY_HAVE_1000:
                if (ctx->GetOption(RSK_HBA_HINT)) {
                    auto item1000_loc = ctx->GetItemLocation(RC_GF_HBA_1000_POINTS);
                    if (item1000_loc->GetCheckStatus() == RCSHOW_UNCHECKED) {
                        item1000_loc->SetCheckStatus(RCSHOW_IDENTIFIED);
                    }
                    reveal = RC_GF_HBA_1500_POINTS;
                }
                break;
            case TEXT_SCRUB_RANDOM:
                if (ctx->GetOption(RSK_SCRUB_TEXT_HINT).Get() != RO_GENERIC_OFF) {
                    EnDns* enDns = (EnDns*)actor;
                    auto checkIdentity = ObjectExtension::GetInstance().Get<ScrubIdentity>(actor);
                    if (checkIdentity != nullptr) {
                        reveal = OTRGlobals::Instance->gRandomizer->GetCheckFromRandomizerInf(
                            checkIdentity->identity.randomizerInf);
                    }
                }
                break;
            case TEXT_BEAN_SALESMAN_BUY_FOR_10:
                if (revealMerchant && (ctx->GetOption(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_BEANS_ONLY) ||
                                       ctx->GetOption(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL))) {
                    reveal = RC_ZR_MAGIC_BEAN_SALESMAN;
                }
                break;
            case TEXT_GRANNYS_SHOP:
                if (revealMerchant && nonBeanMerchants &&
                    (ctx->GetOption(RSK_SHUFFLE_ADULT_TRADE) || INV_CONTENT(ITEM_CLAIM_CHECK) == ITEM_CLAIM_CHECK)) {
                    reveal = RC_KAK_GRANNYS_SHOP;
                }
                break;
            case TEXT_MEDIGORON:
                if (revealMerchant && nonBeanMerchants) {
                    reveal = RC_GC_MEDIGORON;
                }
                break;
            case TEXT_CARPET_SALESMAN_1:
                if (revealMerchant && nonBeanMerchants) {
                    reveal = RC_WASTELAND_BOMBCHU_SALESMAN;
                }
                break;
            case TEXT_BIGGORON_BETTER_AT_SMITHING:
            case TEXT_BIGGORON_WAITING_FOR_YOU:
            case TEXT_BIGGORON_RETURN_AFTER_A_FEW_DAYS:
            case TEXT_BIGGORON_I_MAAAADE_THISSSS:
                if (ctx->GetOption(RSK_BIGGORON_HINT)) {
                    reveal = RC_DMT_TRADE_CLAIM_CHECK;
                }
                break;
            case TEXT_SHEIK_NEED_HOOK:
            case TEXT_SHEIK_HAVE_HOOK:
                if (ctx->GetOption(RSK_OOT_HINT) && gPlayState->sceneNum == SCENE_TEMPLE_OF_TIME &&
                    !ctx->GetItemLocation(RC_SONG_FROM_OCARINA_OF_TIME)->HasObtained()) {
                    auto itemoot_loc = ctx->GetItemLocation(RC_HF_OCARINA_OF_TIME_ITEM);
                    if (itemoot_loc->GetCheckStatus() == RCSHOW_UNCHECKED) {
                        itemoot_loc->SetCheckStatus(RCSHOW_IDENTIFIED);
                    }
                    reveal = RC_SONG_FROM_OCARINA_OF_TIME;
                }
                break;
            case TEXT_FISHING_CLOUDY:
            case TEXT_FISHING_TRY_ANOTHER_LURE:
            case TEXT_FISHING_SECRETS:
            case TEXT_FISHING_GOOD_FISHERMAN:
            case TEXT_FISHING_DIFFERENT_POND:
            case TEXT_FISHING_SCRATCHING:
            case TEXT_FISHING_TRY_ANOTHER_LURE_WITH_SINKING_LURE:
                if (ctx->GetOption(RSK_LOACH_HINT)) {
                    reveal = RC_LH_HYRULE_LOACH;
                }
                break;
        }
    }

    if (reveal != RC_UNKNOWN_CHECK) {
        auto item_loc = ctx->GetItemLocation(reveal);
        if (item_loc->GetCheckStatus() == RCSHOW_UNCHECKED) {
            item_loc->SetCheckStatus(RCSHOW_IDENTIFIED);
        }
    }
}
