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
#include "randomizer_runtime_lifecycle_hooks.h"
#include "randomizer_item_delivery_hooks.h"

void RandomizerOnGameFrameUpdateHandler() {
    if (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_QUIVER)) {
        AMMO(ITEM_BOW) = static_cast<int8_t>(CUR_CAPACITY(UPG_QUIVER));
    }

    if (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_BOMB_BAG)) {
        AMMO(ITEM_BOMB) = static_cast<int8_t>(CUR_CAPACITY(UPG_BOMB_BAG));
    }

    if (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_BULLET_BAG)) {
        AMMO(ITEM_SLINGSHOT) = static_cast<int8_t>(CUR_CAPACITY(UPG_BULLET_BAG));
    }

    if (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_STICK_UPGRADE)) {
        AMMO(ITEM_STICK) = static_cast<int8_t>(CUR_CAPACITY(UPG_STICKS));
    }

    if (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_NUT_UPGRADE)) {
        AMMO(ITEM_NUT) = static_cast<int8_t>(CUR_CAPACITY(UPG_NUTS));
    }

    if (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_MAGIC_METER)) {
        gSaveContext.magic = static_cast<int8_t>(gSaveContext.magicCapacity);
    }

    if (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_BOMBCHUS)) {
        AMMO(ITEM_BOMBCHU) = 50;
    }

    if (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_MONEY)) {
        gSaveContext.rupees = static_cast<s16>(CUR_CAPACITY(UPG_WALLET));
    }

    if (!Flags_GetRandomizerInf(RAND_INF_HAS_WALLET)) {
        gSaveContext.rupees = 0;
    }
}

extern "C" void func_8099485C(DoorGerudo* gerudoDoor, PlayState* play);

void RandomizerOnActorUpdateHandler(void* refActor) {
    Actor* actor = static_cast<Actor*>(refActor);

    if (Flags_GetRandomizerInf(RAND_INF_HAS_SKELETON_KEY)) {
        if (actor->id == ACTOR_EN_DOOR) {
            EnDoor* door = reinterpret_cast<EnDoor*>(actor);
            door->lockTimer = 0;
        } else if (actor->id == ACTOR_DOOR_SHUTTER) {
            DoorShutter* shutterDoor = reinterpret_cast<DoorShutter*>(actor);
            if (shutterDoor->doorType == SHUTTER_KEY_LOCKED) {
                shutterDoor->unlockTimer = 0;
            }
        } else if (actor->id == ACTOR_DOOR_GERUDO) {
            DoorGerudo* gerudoDoor = reinterpret_cast<DoorGerudo*>(actor);
            gerudoDoor->actionFunc = func_8099485C;
            gerudoDoor->dyna.actor.world.pos.y = gerudoDoor->dyna.actor.home.pos.y + 200.0f;
        }
    }

    if (actor->id == ACTOR_BG_JYA_BIGMIRROR) {
        auto jyaBigMirror = reinterpret_cast<BgJyaBigmirror*>(actor);
        if ((jyaBigMirror->puzzleFlags & (BIGMIR_PUZZLE_COBRA1_SOLVED | BIGMIR_PUZZLE_COBRA2_SOLVED)) ==
            (BIGMIR_PUZZLE_COBRA1_SOLVED | BIGMIR_PUZZLE_COBRA2_SOLVED)) {
            Flags_SetRandomizerInf(RAND_INF_SPIRIT_BIG_MIRROR_STATUE_TURNED);
        } else {
            Flags_UnsetRandomizerInf(RAND_INF_SPIRIT_BIG_MIRROR_STATUE_TURNED);
        }
    }

    // In ER, override the warp song locations. Also removes the warp song cutscene
    if (RAND_GET_OPTION(RSK_SHUFFLE_ENTRANCES) && actor->id == ACTOR_DEMO_KANKYO &&
        actor->params == 0x000F) { // Warp Song particles
        Entrance_SetWarpSongEntrance();
    }
}

// from z_player.c
typedef struct {
    /* 0x00 */ Vec3f pos;
    /* 0x0C */ s16 yaw;
} SpecialRespawnInfo; // size = 0x10

// special respawns used when voided out without swim to prevent infinite loops
std::unordered_map<s32, SpecialRespawnInfo> swimSpecialRespawnInfo = {
    { ENTR_ZORAS_RIVER_3, // hf to zr in water
      { { -1455.443f, -20.0f, 1384.826f }, 28761 } },
    { ENTR_HYRULE_FIELD_14, // zr to hf in water
      { { 5730.209f, -20.0f, 3725.911f }, -20025 } },
    { ENTR_LOST_WOODS_UNDERWATER_SHORTCUT, // zr to lw
      { { 1978.718f, -36.908f, -855.0f }, -16384 } },
    { ENTR_ZORAS_RIVER_UNDERWATER_SHORTCUT, // lw to zr
      { { 4082.366f, 860.442f, -1018.949f }, -32768 } },
    { ENTR_LAKE_HYLIA_RIVER_EXIT, // gv to lh
      { { -3276.416f, -1033.0f, 2908.421f }, 11228 } },
    { ENTR_WATER_TEMPLE_ENTRANCE, // lh to water temple
      { { -182.0f, 780.0f, 759.5f }, -32768 } },
    { ENTR_LAKE_HYLIA_OUTSIDE_TEMPLE, // water temple to lh
      { { -955.028f, -1306.9f, 6768.954f }, -32768 } },
    { ENTR_ZORAS_DOMAIN_UNDERWATER_SHORTCUT, // lh to zd
      { { -109.86f, 11.396f, -9.933f }, -29131 } },
    { ENTR_LAKE_HYLIA_UNDERWATER_SHORTCUT, // zd to lh
      { { -912.0f, -1326.967f, 3391.0f }, 0 } },
    { ENTR_GERUDO_VALLEY_1, // caught by gerudos as child
      { { -424.0f, -2051.0f, -74.0f }, 16384 } },
    { ENTR_HYRULE_FIELD_ON_BRIDGE_SPAWN, // mk to hf (can be a problem when it then turns night)
      { { 0.0f, 0.0f, 1100.0f }, 0 } },
    { ENTR_ZORAS_FOUNTAIN_JABU_JABU_BLUE_WARP, // jabu blue warp to zf
      { { -1580.0f, 150.0f, 1670.0f }, 8000 } },
};

f32 triforcePieceScale;

void RandomizerOnPlayerUpdateHandler() {
    if ((GET_PLAYER(gPlayState)->stateFlags1 & PLAYER_STATE1_IN_WATER) && !Flags_GetRandomizerInf(RAND_INF_CAN_SWIM) &&
        CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS) != EQUIP_VALUE_BOOTS_IRON) {
        // if you void out in water temple without swim you get instantly kicked out to prevent softlocks
        if (gPlayState->sceneNum == SCENE_WATER_TEMPLE) {
            GameInteractor::RawAction::TeleportPlayer(
                Entrance_OverrideNextIndex(ENTR_LAKE_HYLIA_OUTSIDE_TEMPLE)); // lake hylia from water temple
        } else {
            auto respawn = swimSpecialRespawnInfo.find(gSaveContext.entranceIndex);
            if (respawn != swimSpecialRespawnInfo.end()) {
                Play_SetupRespawnPoint(gPlayState, RESPAWN_MODE_DOWN, 0xDFF);
                if (gPlayState->sceneNum == gEntranceTable[gSaveContext.entranceIndex].scene) {
                    gSaveContext.respawn[RESPAWN_MODE_DOWN].roomIndex =
                        gPlayState->setupEntranceList[gEntranceTable[gSaveContext.entranceIndex].spawn].room;
                }
                gSaveContext.respawn[RESPAWN_MODE_DOWN].pos = respawn->second.pos;
                gSaveContext.respawn[RESPAWN_MODE_DOWN].yaw = respawn->second.yaw;
            }

            if (gPlayState->sceneNum == SCENE_GROTTOS) {
                // RESPAWN_MODE_DOWN isn't refreshed on grotto entry, reload grotto instead
                gPlayState->nextEntranceIndex = gSaveContext.entranceIndex;
                gPlayState->transitionTrigger = TRANS_TRIGGER_START;
                gPlayState->transitionType = TRANS_TYPE_FADE_BLACK;
                gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
                gSaveContext.respawnFlag = 0;
            } else {
                Play_TriggerVoidOut(gPlayState);
            }
        }
    }

    // Triforce Hunt needs the check if the player isn't being teleported to the credits scene.
    if (!GameInteractor::IsGameplayPaused() && Flags_GetRandomizerInf(RAND_INF_GRANT_GANONS_BOSSKEY) &&
        gPlayState->transitionTrigger != TRANS_TRIGGER_START &&
        (1 << 0 & gSaveContext.inventory.dungeonItems[SCENE_GANONS_TOWER]) == 0) {
        GiveItemEntryWithoutActor(gPlayState,
                                  *Rando::StaticData::GetItemTable().at(RG_GANONS_CASTLE_BOSS_KEY).GetGIEntry());
    }

    if (!GameInteractor::IsGameplayPaused() && RAND_GET_OPTION(RSK_TRIFORCE_HUNT).IsNot(RO_TRIFORCE_HUNT_OFF)) {
        // Warp to credits once item queue has drained to avoid losing queued items
        if (GameInteractor::State::TriforceHuntCreditsWarpActive && RandomizerItemQueueIsDrained()) {
            gSaveContext.ship.stats.itemTimestamp[TIMESTAMP_TRIFORCE_COMPLETED] =
                static_cast<u32>(GAMEPLAYSTAT_TOTAL_TIME);
            gSaveContext.ship.stats.gameComplete = 1;
            Play_PerformSave(gPlayState);
            Notification::Emit({ .message = "Game autosaved" });
            gPlayState->nextEntranceIndex = ENTR_CHAMBER_OF_THE_SAGES_0;
            gSaveContext.nextCutsceneIndex = 0xFFF2;
            gPlayState->transitionTrigger = TRANS_TRIGGER_START;
            gPlayState->transitionType = TRANS_TYPE_FADE_WHITE;
            GameInteractor::State::TriforceHuntCreditsWarpActive = 0;
        }

        // Reset Triforce Piece scale for GI animation. Triforce Hunt allows for multiple triforce models,
        // and cycles through them based on the amount of triforce pieces collected. It takes a little while
        // for the count to increase during the GI animation, so the model is entirely hidden until that piece
        // has been added. That scale has to be reset after the textbox is closed, and this is the best way
        // to ensure it's done at that point in time specifically.
        if (GameInteractor::State::TriforceHuntPieceGiven) {
            triforcePieceScale = 0.0f;
            GameInteractor::State::TriforceHuntPieceGiven = 0;
        }
    }
}

void RandomizerOnSceneSpawnActorsHandler() {
    if (LINK_IS_ADULT && RAND_GET_OPTION(RSK_SHEIK_LA_HINT)) {
        switch (gPlayState->sceneNum) {
            case SCENE_TEMPLE_OF_TIME:
                if (gPlayState->roomCtx.curRoom.num == 1) {
                    Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_XC, -104, -40, 2382, 0,
                                static_cast<int16_t>(0x8000), 0, SHEIK_TYPE_RANDO);
                }
                break;
            case SCENE_INSIDE_GANONS_CASTLE:
                if (gPlayState->roomCtx.curRoom.num == 1) {
                    Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_XC, 101, 150, 137, 0, 0, 0,
                                SHEIK_TYPE_RANDO);
                }
                break;
            default:
                break;
        }
    }
}

void RandomizerOnPlayDestroyHandler() {
    // In ER, remove link from epona when entering somewhere that doesn't support epona
    if (RAND_GET_OPTION(RSK_SHUFFLE_OVERWORLD_ENTRANCES)) {
        Entrance_HandleEponaState();
    }
}

void RandomizerOnExitGameHandler(int32_t fileNum) {
    // When going from a rando save to a vanilla save within the same game instance
    // we need to reset the entrance table back to its vanilla state
    Entrance_ResetEntranceTable();
}

void RandomizerOnKaleidoscopeUpdateHandler(int16_t inDungeonScene) {
    static uint16_t prevKaleidoState = 0;

    // In ER, handle overriding the game over respawn entrance and dealing with death warp to from grottos
    if (RAND_GET_OPTION(RSK_SHUFFLE_ENTRANCES)) {
        if (prevKaleidoState == 0x10 && gPlayState->pauseCtx.state == 0x11 && gPlayState->pauseCtx.promptChoice == 0) {
            // Needs to be called before Play_TriggerRespawn when transitioning from state 0x10 to 0x11
            Entrance_SetGameOverEntrance();
        }
        if (prevKaleidoState == 0x11 && gPlayState->pauseCtx.state == 0 && gPlayState->pauseCtx.promptChoice == 0) {
            // Needs to be called after Play_TriggerRespawn when transitioning from state 0x11 to 0
            Grotto_ForceGrottoReturn();
        }
    }

    prevKaleidoState = gPlayState->pauseCtx.state;
}

void RandomizerOnCuccoOrChickenHatch() {
    if (LINK_IS_CHILD) {
        Flags_UnsetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_WEIRD_EGG);
        Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_CHICKEN);
    }
}
