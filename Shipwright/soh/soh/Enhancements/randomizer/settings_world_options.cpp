#include "soh/OTRGlobals.h"
#include "settings.h"
#include <cstdio>
#include "trial.h"
#include "dungeon.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/randomizer/3drando/random.hpp"

#include <spdlog/spdlog.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/libultraship.h>
#include "settings_option_registry_macros.h"

namespace Rando {

void Settings::CreateWorldAndShuffleOptions() {
    // clang-format off
    OPT_U8(RSK_FOREST, "Closed Forest", {"On", "Deku Only", "Off"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ClosedForest"), mOptionDescriptions[RSK_FOREST], WIDGET_CVAR_COMBOBOX, RO_CLOSED_FOREST_ON);
    OPT_CALLBACK(RSK_FOREST, {
        HandleStartingAgeUI();
    });
    OPT_U8(RSK_KAK_GATE, "Kakariko Gate", {"Closed", "Open"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("KakarikoGate"), mOptionDescriptions[RSK_KAK_GATE]);
    OPT_U8(RSK_DOOR_OF_TIME, "Door of Time", {"Closed", "Song only", "Open"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("DoorOfTime"), mOptionDescriptions[RSK_DOOR_OF_TIME], WIDGET_CVAR_COMBOBOX);
    OPT_CALLBACK(RSK_DOOR_OF_TIME, {
        HandleStartingAgeUI();
    });
    OPT_U8(RSK_ZORAS_FOUNTAIN, "Zora's Fountain", {"Closed", "Closed as child", "Open"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ZorasFountain"), mOptionDescriptions[RSK_ZORAS_FOUNTAIN]);
    OPT_U8(RSK_SLEEPING_WATERFALL, "Sleeping Waterfall", {"Closed", "Open"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("SleepingWaterfall"), mOptionDescriptions[RSK_SLEEPING_WATERFALL]);
    OPT_U8(RSK_JABU_OPEN, "Jabu-Jabu", {"Closed", "Open"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("JabuJabu"), mOptionDescriptions[RSK_JABU_OPEN]);
    OPT_BOOL(RSK_LOCK_OVERWORLD_DOORS, "Lock Overworld Doors", CVAR_RANDOMIZER_SETTING("LockOverworldDoors"), mOptionDescriptions[RSK_LOCK_OVERWORLD_DOORS]);
    OPT_U8(RSK_GERUDO_FORTRESS, "Fortress Carpenters", {"Normal", "Fast", "Free"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("FortressCarpenters"), mOptionDescriptions[RSK_GERUDO_FORTRESS]);
    OPT_CALLBACK(RSK_GERUDO_FORTRESS, {
        const uint8_t maxKeyringCount =
            (CVarGetInteger(CVAR_RANDOMIZER_SETTING("FortressCarpenters"), RO_GF_CARPENTERS_NORMAL) ==
                RO_GF_CARPENTERS_NORMAL &&
            CVarGetInteger(CVAR_RANDOMIZER_SETTING("GerudoKeys"), RO_GERUDO_KEYS_VANILLA) != RO_GERUDO_KEYS_VANILLA)
                ? 9
                : 8;
        if (mOptions[RSK_KEYRINGS_RANDOM_COUNT].GetOptionCount() != maxKeyringCount + 1) {
            mOptions[RSK_KEYRINGS_RANDOM_COUNT].ChangeOptions(NumOpts(0, maxKeyringCount));
        }
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("FortressCarpenters"), RO_GF_CARPENTERS_NORMAL) !=
                RO_GF_CARPENTERS_NORMAL ||
            CVarGetInteger(CVAR_RANDOMIZER_SETTING("GerudoKeys"), RO_GERUDO_KEYS_VANILLA) == RO_GERUDO_KEYS_VANILLA) {
            mOptions[RSK_KEYRINGS_GERUDO_FORTRESS].Disable(
                "Disabled because the currently selected Gerudo Fortress Carpenters\n"
                "setting and/or Gerudo Fortress Keys setting is incompatible with\n"
                "having a Gerudo Fortress Keyring.");
        } else {
            mOptions[RSK_KEYRINGS_GERUDO_FORTRESS].Enable();
        }
    });
    OPT_U8(RSK_RAINBOW_BRIDGE, "Rainbow Bridge", {"Vanilla", "Always open", "Stones", "Medallions", "Dungeon rewards", "Dungeons", "Tokens", "Greg"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("RainbowBridge"), mOptionDescriptions[RSK_RAINBOW_BRIDGE], WIDGET_CVAR_COMBOBOX, RO_BRIDGE_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_CALLBACK(RSK_RAINBOW_BRIDGE, {
        mOptions[RSK_BRIDGE_OPTIONS].Hide();
        mOptions[RSK_RAINBOW_BRIDGE_STONE_COUNT].Hide();
        mOptions[RSK_RAINBOW_BRIDGE_MEDALLION_COUNT].Hide();
        mOptions[RSK_RAINBOW_BRIDGE_REWARD_COUNT].Hide();
        mOptions[RSK_RAINBOW_BRIDGE_DUNGEON_COUNT].Hide();
        mOptions[RSK_RAINBOW_BRIDGE_TOKEN_COUNT].Hide();
        switch (CVarGetInteger(CVAR_RANDOMIZER_SETTING("RainbowBridge"), RO_BRIDGE_VANILLA)) {
            case RO_BRIDGE_STONES:
                // Show Bridge Options and Stone Count slider
                mOptions[RSK_RAINBOW_BRIDGE].RemoveFlag(IMFLAG_SEPARATOR_BOTTOM);
                mOptions[RSK_BRIDGE_OPTIONS].Unhide();
                mOptions[RSK_RAINBOW_BRIDGE_STONE_COUNT].Unhide();
                break;
            case RO_BRIDGE_MEDALLIONS:
                // Show Bridge Options and Medallion Count Slider
                mOptions[RSK_RAINBOW_BRIDGE].RemoveFlag(IMFLAG_SEPARATOR_BOTTOM);
                mOptions[RSK_BRIDGE_OPTIONS].Unhide();
                mOptions[RSK_RAINBOW_BRIDGE_MEDALLION_COUNT].Unhide();
                break;
            case RO_BRIDGE_DUNGEON_REWARDS:
                // Show Bridge Options and Dungeon Reward Count Slider
                mOptions[RSK_RAINBOW_BRIDGE].RemoveFlag(IMFLAG_SEPARATOR_BOTTOM);
                mOptions[RSK_BRIDGE_OPTIONS].Unhide();
                mOptions[RSK_RAINBOW_BRIDGE_REWARD_COUNT].Unhide();
                break;
            case RO_BRIDGE_DUNGEONS:
                // Show Bridge Options and Dungeon Count Slider
                mOptions[RSK_RAINBOW_BRIDGE].RemoveFlag(IMFLAG_SEPARATOR_BOTTOM);
                mOptions[RSK_BRIDGE_OPTIONS].Unhide();
                mOptions[RSK_RAINBOW_BRIDGE_DUNGEON_COUNT].Unhide();
                break;
            case RO_BRIDGE_TOKENS:
                // Show token count slider (not bridge options)
                mOptions[RSK_RAINBOW_BRIDGE].RemoveFlag(IMFLAG_SEPARATOR_BOTTOM);
                mOptions[RSK_BRIDGE_OPTIONS].Hide();
                mOptions[RSK_RAINBOW_BRIDGE_TOKEN_COUNT].Unhide();
                break;
            default:
                break;
        }
    });
    OPT_U8(RSK_RAINBOW_BRIDGE_STONE_COUNT, "Bridge Stone Count", {NumOpts(0, 4)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("StoneCount"), "", WIDGET_CVAR_SLIDER_INT, 3, true);
    OPT_U8(RSK_RAINBOW_BRIDGE_MEDALLION_COUNT, "Bridge Medallion Count", {NumOpts(0, 7)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MedallionCount"), "", WIDGET_CVAR_SLIDER_INT, 6, true);
    OPT_U8(RSK_RAINBOW_BRIDGE_REWARD_COUNT, "Bridge Reward Count", {NumOpts(0, 10)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("RewardCount"), "", WIDGET_CVAR_SLIDER_INT, 9, true);
    OPT_U8(RSK_RAINBOW_BRIDGE_DUNGEON_COUNT, "Bridge Dungeon Count", {NumOpts(0, 9)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("DungeonCount"), "", WIDGET_CVAR_SLIDER_INT, 8, true);
    OPT_U8(RSK_RAINBOW_BRIDGE_TOKEN_COUNT, "Bridge Token Count", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("TokenCount"), "", WIDGET_CVAR_SLIDER_INT, 100, true);
    OPT_U8(RSK_BRIDGE_OPTIONS, "Bridge Reward Options", {"Standard Rewards", "Greg as Reward", "Greg as Wildcard"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("BridgeRewardOptions"), mOptionDescriptions[RSK_BRIDGE_OPTIONS], WIDGET_CVAR_COMBOBOX, RO_BRIDGE_STANDARD_REWARD, false, nullptr, IMFLAG_NONE);
    OPT_CALLBACK(RSK_BRIDGE_OPTIONS, {
        const uint8_t bridgeOpt = CVarGetInteger(CVAR_RANDOMIZER_SETTING("BridgeRewardOptions"), RO_BRIDGE_STANDARD_REWARD);
        if (bridgeOpt == RO_BRIDGE_GREG_REWARD) {
            if (mOptions[RSK_RAINBOW_BRIDGE_STONE_COUNT].GetOptionCount() == 4) {
                mOptions[RSK_RAINBOW_BRIDGE_STONE_COUNT].ChangeOptions(NumOpts(0, 4));
            }
            if (mOptions[RSK_RAINBOW_BRIDGE_MEDALLION_COUNT].GetOptionCount() == 7) {
                mOptions[RSK_RAINBOW_BRIDGE_MEDALLION_COUNT].ChangeOptions(NumOpts(0, 7));
            }
            if (mOptions[RSK_RAINBOW_BRIDGE_REWARD_COUNT].GetOptionCount() == 10) {
                mOptions[RSK_RAINBOW_BRIDGE_REWARD_COUNT].ChangeOptions(NumOpts(0, 10));
            }
            if (mOptions[RSK_RAINBOW_BRIDGE_DUNGEON_COUNT].GetOptionCount() == 9) {
                mOptions[RSK_RAINBOW_BRIDGE_DUNGEON_COUNT].ChangeOptions(NumOpts(0, 9));
            }
        } else {
            if (mOptions[RSK_RAINBOW_BRIDGE_STONE_COUNT].GetOptionCount() == 5) {
                mOptions[RSK_RAINBOW_BRIDGE_STONE_COUNT].ChangeOptions(NumOpts(0, 3));
            }
            if (mOptions[RSK_RAINBOW_BRIDGE_MEDALLION_COUNT].GetOptionCount() == 8) {
                mOptions[RSK_RAINBOW_BRIDGE_MEDALLION_COUNT].ChangeOptions(NumOpts(0, 6));
            }
            if (mOptions[RSK_RAINBOW_BRIDGE_REWARD_COUNT].GetOptionCount() == 11) {
                mOptions[RSK_RAINBOW_BRIDGE_REWARD_COUNT].ChangeOptions(NumOpts(0, 9));
            }
            if (mOptions[RSK_RAINBOW_BRIDGE_DUNGEON_COUNT].GetOptionCount() == 10) {
                mOptions[RSK_RAINBOW_BRIDGE_DUNGEON_COUNT].ChangeOptions(NumOpts(0, 8));
            }
        }
    });
    OPT_U8(RSK_GANONS_TRIALS, "Ganon's Trials", {"Skip", "Set Number", "Random Number"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("GanonTrial"), mOptionDescriptions[RSK_GANONS_TRIALS], WIDGET_CVAR_COMBOBOX, RO_GANONS_TRIALS_SET_NUMBER);
    OPT_CALLBACK(RSK_GANONS_TRIALS, {
        // Only show the trial count slider if Trials is set to Set Number
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("GanonTrial"), RO_GANONS_TRIALS_SET_NUMBER) ==
            RO_GANONS_TRIALS_SET_NUMBER) {
            mOptions[RSK_TRIAL_COUNT].Unhide();
        } else {
            mOptions[RSK_TRIAL_COUNT].Hide();
        }
    });
    OPT_U8(RSK_TRIAL_COUNT, "Ganon's Trials Count", {NumOpts(0, 6)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("GanonTrialCount"), mOptionDescriptions[RSK_TRIAL_COUNT], WIDGET_CVAR_SLIDER_INT, 6, true);
    OPT_BOOL(RSK_MEDALLION_LOCKED_TRIALS, "Medallion Locked Trials", CVAR_RANDOMIZER_SETTING("MedallionLockedTrials"), mOptionDescriptions[RSK_MEDALLION_LOCKED_TRIALS]);
    OPT_U8(RSK_STARTING_AGE, "Starting Age", {"Child", "Adult", "Random"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("StartingAge"), mOptionDescriptions[RSK_STARTING_AGE], WIDGET_CVAR_COMBOBOX, RO_AGE_CHILD);
    OPT_U8(RSK_SELECTED_STARTING_AGE, "Selected Starting Age", {"Child", "Adult"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("SelectedStartingAge"), mOptionDescriptions[RSK_STARTING_AGE], WIDGET_CVAR_COMBOBOX, RO_AGE_CHILD);
    OPT_BOOL(RSK_SHUFFLE_ENTRANCES, "Shuffle Entrances");
    OPT_U8(RSK_SHUFFLE_DUNGEON_ENTRANCES, "Dungeon Entrances", {"Off", "On", "On + Ganon"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleDungeonsEntrances"), mOptionDescriptions[RSK_SHUFFLE_DUNGEON_ENTRANCES], WIDGET_CVAR_COMBOBOX, RO_DUNGEON_ENTRANCE_SHUFFLE_OFF);
    OPT_CALLBACK(RSK_SHUFFLE_DUNGEON_ENTRANCES, {
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleDungeonsEntrances"), RO_DUNGEON_ENTRANCE_SHUFFLE_OFF) ==
            RO_DUNGEON_ENTRANCE_SHUFFLE_OFF ||
            CVarGetInteger(CVAR_RANDOMIZER_SETTING("MixedEntrances"), RO_GENERIC_OFF) == RO_GENERIC_OFF) {
            mOptions[RSK_MIX_DUNGEON_ENTRANCES].Hide();
        } else {
            mOptions[RSK_MIX_DUNGEON_ENTRANCES].Unhide();
        }
    });
    OPT_U8(RSK_SHUFFLE_BOSS_ENTRANCES, "Boss Entrances", {"Off", "Age Restricted", "Full"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleBossEntrances"), mOptionDescriptions[RSK_SHUFFLE_BOSS_ENTRANCES], WIDGET_CVAR_COMBOBOX, RO_BOSS_ROOM_ENTRANCE_SHUFFLE_OFF);
    OPT_CALLBACK(RSK_SHUFFLE_BOSS_ENTRANCES, {
        HandleMixedEntrancePoolsUI();

        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleBossEntrances"), RO_BOSS_ROOM_ENTRANCE_SHUFFLE_OFF) == RO_BOSS_ROOM_ENTRANCE_SHUFFLE_OFF) {
            mOptions[RSK_SHUFFLE_GANONS_TOWER_ENTRANCE].Hide();
        } else {
            mOptions[RSK_SHUFFLE_GANONS_TOWER_ENTRANCE].Unhide();
        }

        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleBossEntrances"), RO_BOSS_ROOM_ENTRANCE_SHUFFLE_OFF) ==
            RO_BOSS_ROOM_ENTRANCE_SHUFFLE_OFF ||
            CVarGetInteger(CVAR_RANDOMIZER_SETTING("MixedEntrances"), RO_GENERIC_OFF) == RO_GENERIC_OFF) {
            mOptions[RSK_MIX_BOSS_ENTRANCES].Hide();
        } else {
            mOptions[RSK_MIX_BOSS_ENTRANCES].Unhide();
        }
    });
    OPT_BOOL(RSK_SHUFFLE_GANONS_TOWER_ENTRANCE, "Ganon's Tower Entrance", CVAR_RANDOMIZER_SETTING("ShuffleGanonTowerEntrance"), mOptionDescriptions[RSK_SHUFFLE_GANONS_TOWER_ENTRANCE]);
    OPT_BOOL(RSK_SHUFFLE_OVERWORLD_ENTRANCES, "Overworld Entrances", CVAR_RANDOMIZER_SETTING("ShuffleOverworldEntrances"), mOptionDescriptions[RSK_SHUFFLE_OVERWORLD_ENTRANCES]);
    OPT_CALLBACK(RSK_SHUFFLE_OVERWORLD_ENTRANCES, {
        HandleMixedEntrancePoolsUI();

        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleOverworldEntrances"), RO_GENERIC_OFF) == RO_GENERIC_OFF ||
            CVarGetInteger(CVAR_RANDOMIZER_SETTING("MixedEntrances"), RO_GENERIC_OFF) == RO_GENERIC_OFF) {
            mOptions[RSK_MIX_OVERWORLD_ENTRANCES].Hide();
        } else {
            mOptions[RSK_MIX_OVERWORLD_ENTRANCES].Unhide();
        }

        HandleStartingAgeUI();
    });
    OPT_U8(RSK_SHUFFLE_INTERIOR_ENTRANCES, "Interior Entrances", {"Off", "Simple", "All"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleInteriorsEntrances"), mOptionDescriptions[RSK_SHUFFLE_INTERIOR_ENTRANCES], WIDGET_CVAR_COMBOBOX, RO_INTERIOR_ENTRANCE_SHUFFLE_OFF);
    OPT_CALLBACK(RSK_SHUFFLE_INTERIOR_ENTRANCES, {
        HandleMixedEntrancePoolsUI();

        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleInteriorsEntrances"), RO_GENERIC_OFF) == RO_GENERIC_OFF ||
            CVarGetInteger(CVAR_RANDOMIZER_SETTING("MixedEntrances"), RO_GENERIC_OFF) == RO_GENERIC_OFF) {
            mOptions[RSK_MIX_INTERIOR_ENTRANCES].Hide();
        } else {
            mOptions[RSK_MIX_INTERIOR_ENTRANCES].Unhide();
        }

        HandleStartingAgeUI();
    });
    OPT_BOOL(RSK_SHUFFLE_THIEVES_HIDEOUT_ENTRANCES, "Thieves' Hideout Entrances", CVAR_RANDOMIZER_SETTING("ShuffleThievesHideoutEntrances"), mOptionDescriptions[RSK_SHUFFLE_THIEVES_HIDEOUT_ENTRANCES]);
    OPT_CALLBACK(RSK_SHUFFLE_THIEVES_HIDEOUT_ENTRANCES, {
        HandleMixedEntrancePoolsUI();

        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleThievesHideoutEntrances"), RO_GENERIC_OFF) ==
            RO_GENERIC_OFF ||
            CVarGetInteger(CVAR_RANDOMIZER_SETTING("MixedEntrances"), RO_GENERIC_OFF) == RO_GENERIC_OFF) {
            mOptions[RSK_MIX_THIEVES_HIDEOUT_ENTRANCES].Hide();
        } else {
            mOptions[RSK_MIX_THIEVES_HIDEOUT_ENTRANCES].Unhide();
        }
    });
    OPT_BOOL(RSK_SHUFFLE_GROTTO_ENTRANCES, "Grottos Entrances", CVAR_RANDOMIZER_SETTING("ShuffleGrottosEntrances"), mOptionDescriptions[RSK_SHUFFLE_GROTTO_ENTRANCES]);
    OPT_CALLBACK(RSK_SHUFFLE_GROTTO_ENTRANCES, {
        HandleMixedEntrancePoolsUI();

        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleGrottosEntrances"), RO_GENERIC_OFF) == RO_GENERIC_OFF ||
            CVarGetInteger(CVAR_RANDOMIZER_SETTING("MixedEntrances"), RO_GENERIC_OFF) == RO_GENERIC_OFF) {
            mOptions[RSK_MIX_GROTTO_ENTRANCES].Hide();
        } else {
            mOptions[RSK_MIX_GROTTO_ENTRANCES].Unhide();
        }

        HandleStartingAgeUI();
    });
    OPT_BOOL(RSK_SHUFFLE_OWL_DROPS, "Owl Drops", CVAR_RANDOMIZER_SETTING("ShuffleOwlDrops"), mOptionDescriptions[RSK_SHUFFLE_OWL_DROPS]);
    OPT_BOOL(RSK_SHUFFLE_WARP_SONGS, "Warp Songs", CVAR_RANDOMIZER_SETTING("ShuffleWarpSongs"), mOptionDescriptions[RSK_SHUFFLE_WARP_SONGS]);
    OPT_CALLBACK(RSK_SHUFFLE_WARP_SONGS, {
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleWarpSongs"), RO_GENERIC_ON)) {
            mOptions[RSK_WARP_SONG_HINTS].Enable();
        } else {
            mOptions[RSK_WARP_SONG_HINTS].Disable("This option is disabled since warp song locations are not shuffled.");
        }
    });
    OPT_BOOL(RSK_SHUFFLE_OVERWORLD_SPAWNS, "Overworld Spawns", CVAR_RANDOMIZER_SETTING("ShuffleOverworldSpawns"), mOptionDescriptions[RSK_SHUFFLE_OVERWORLD_SPAWNS]);
    OPT_CALLBACK(RSK_SHUFFLE_OVERWORLD_SPAWNS, {
        HandleStartingAgeUI();
    });
    OPT_BOOL(RSK_MIXED_ENTRANCE_POOLS, "Mixed Entrance Pools", CVAR_RANDOMIZER_SETTING("MixedEntrances"), mOptionDescriptions[RSK_MIXED_ENTRANCE_POOLS]);
    OPT_CALLBACK(RSK_MIXED_ENTRANCE_POOLS, {
        // Show mixed entrance pool options if mixed entrance pools are enabled, but only the ones that aren't off
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("MixedEntrances"), RO_GENERIC_OFF) == RO_GENERIC_OFF ||
            mOptions[RSK_MIXED_ENTRANCE_POOLS].IsHidden()) {
            mOptions[RSK_MIXED_ENTRANCE_POOLS].AddFlag(IMFLAG_SEPARATOR_BOTTOM);
            mOptions[RSK_MIX_DUNGEON_ENTRANCES].Hide();
            mOptions[RSK_MIX_BOSS_ENTRANCES].Hide();
            mOptions[RSK_MIX_OVERWORLD_ENTRANCES].Hide();
            mOptions[RSK_MIX_INTERIOR_ENTRANCES].Hide();
            mOptions[RSK_MIX_THIEVES_HIDEOUT_ENTRANCES].Hide();
            mOptions[RSK_MIX_GROTTO_ENTRANCES].Hide();
        } else {
            if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleDungeonsEntrances"), RO_DUNGEON_ENTRANCE_SHUFFLE_OFF) !=
                RO_DUNGEON_ENTRANCE_SHUFFLE_OFF) {
                mOptions[RSK_MIX_DUNGEON_ENTRANCES].Unhide();
            }
            if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleBossEntrances"), RO_BOSS_ROOM_ENTRANCE_SHUFFLE_OFF) !=
                RO_BOSS_ROOM_ENTRANCE_SHUFFLE_OFF) {
                mOptions[RSK_MIX_BOSS_ENTRANCES].Unhide();
            }
            if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleOverworldEntrances"), RO_GENERIC_OFF) != RO_GENERIC_OFF) {
                mOptions[RSK_MIX_OVERWORLD_ENTRANCES].Unhide();
            }
            if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleInteriorsEntrances"), RO_GENERIC_OFF) != RO_GENERIC_OFF) {
                mOptions[RSK_MIX_INTERIOR_ENTRANCES].Unhide();
            }
            if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleThievesHideoutEntrances"), RO_GENERIC_OFF) != RO_GENERIC_OFF) {
                mOptions[RSK_MIX_THIEVES_HIDEOUT_ENTRANCES].Unhide();
            }
            if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleGrottosEntrances"), RO_GENERIC_OFF) != RO_GENERIC_OFF) {
                mOptions[RSK_MIX_GROTTO_ENTRANCES].Unhide();
            }
        }
    });
    OPT_BOOL(RSK_MIX_DUNGEON_ENTRANCES, "Mix Dungeons", CVAR_RANDOMIZER_SETTING("MixDungeons"), mOptionDescriptions[RSK_MIX_DUNGEON_ENTRANCES], IMFLAG_NONE);
    OPT_BOOL(RSK_MIX_BOSS_ENTRANCES, "Mix Bosses", CVAR_RANDOMIZER_SETTING("MixBosses"), mOptionDescriptions[RSK_MIX_BOSS_ENTRANCES], IMFLAG_NONE);
    OPT_BOOL(RSK_MIX_OVERWORLD_ENTRANCES, "Mix Overworld", CVAR_RANDOMIZER_SETTING("MixOverworld"), mOptionDescriptions[RSK_MIX_OVERWORLD_ENTRANCES], IMFLAG_NONE);
    OPT_BOOL(RSK_MIX_INTERIOR_ENTRANCES, "Mix Interiors", CVAR_RANDOMIZER_SETTING("MixInteriors"), mOptionDescriptions[RSK_MIX_INTERIOR_ENTRANCES], IMFLAG_NONE);
    OPT_BOOL(RSK_MIX_THIEVES_HIDEOUT_ENTRANCES, "Mix Thieves' Hideout", CVAR_RANDOMIZER_SETTING("MixThievesHideout"), mOptionDescriptions[RSK_MIX_THIEVES_HIDEOUT_ENTRANCES]);
    OPT_BOOL(RSK_MIX_GROTTO_ENTRANCES, "Mix Grottos", CVAR_RANDOMIZER_SETTING("MixGrottos"), mOptionDescriptions[RSK_MIX_GROTTO_ENTRANCES]);
    OPT_BOOL(RSK_DECOUPLED_ENTRANCES, "Decouple Entrances", CVAR_RANDOMIZER_SETTING("DecoupleEntrances"), mOptionDescriptions[RSK_DECOUPLED_ENTRANCES]);
    OPT_CALLBACK(RSK_DECOUPLED_ENTRANCES, {
        HandleStartingAgeUI();
    });
    OPT_U8(RSK_BOMBCHU_BAG, "Bombchu Bag", {"None", "Single Bag", "Progressive Bags"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("BombchuBag"), mOptionDescriptions[RSK_BOMBCHU_BAG], WIDGET_CVAR_COMBOBOX, RO_BOMBCHU_BAG_NONE);
    OPT_U8(RSK_ENABLE_BOMBCHU_DROPS, "Bombchu Drops", {"No", "Yes"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("EnableBombchuDrops"), mOptionDescriptions[RSK_ENABLE_BOMBCHU_DROPS], WIDGET_CVAR_COMBOBOX, RO_AMMO_DROPS_ON);
    // TODO: AmmoDrops and/or HeartDropRefill, combine with/separate Ammo Drops from Bombchu Drops?
    OPT_U8(RSK_TRIFORCE_HUNT, "Triforce Hunt", {"Off", "Win", "Ganon's Boss Key"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("TriforceHunt"), mOptionDescriptions[RSK_TRIFORCE_HUNT]);
    OPT_CALLBACK(RSK_TRIFORCE_HUNT, {
        // Remove the pieces required/total sliders and add a separator after Triforce Hunt if Triforce Hunt is off
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("TriforceHunt"), RO_TRIFORCE_HUNT_OFF) == RO_TRIFORCE_HUNT_OFF) {
            mOptions[RSK_TRIFORCE_HUNT_PIECES_REQUIRED].Hide();
            mOptions[RSK_TRIFORCE_HUNT_PIECES_TOTAL].Hide();
            mOptions[RSK_TRIFORCE_HUNT_PIECES_LOCATION].Hide();
            mOptions[RSK_GANONS_BOSS_KEY].Enable();
        } else {
            mOptions[RSK_TRIFORCE_HUNT_PIECES_REQUIRED].Unhide();
            mOptions[RSK_TRIFORCE_HUNT_PIECES_TOTAL].Unhide();
            mOptions[RSK_TRIFORCE_HUNT_PIECES_LOCATION].Unhide();
            mOptions[RSK_GANONS_BOSS_KEY].Disable(
                "This option is disabled because Triforce Hunt is enabled."
                "Ganon's Boss key\nwill instead be given to you after Triforce Hunt completion.");
        }
    });
    OPT_U8(RSK_TRIFORCE_HUNT_PIECES_TOTAL, "Triforce Hunt Total Pieces", {NumOpts(1, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("TriforceHuntTotalPieces"), mOptionDescriptions[RSK_TRIFORCE_HUNT_PIECES_TOTAL], WIDGET_CVAR_SLIDER_INT, 29, false, nullptr, IMFLAG_NONE);
    OPT_CALLBACK(RSK_TRIFORCE_HUNT_PIECES_TOTAL, {
        // Update triforce pieces required to be capped at the current value for pieces total.
        const uint8_t triforceTotal = CVarGetInteger(CVAR_RANDOMIZER_SETTING("TriforceHuntTotalPieces"), 30);
        if (mOptions[RSK_TRIFORCE_HUNT_PIECES_REQUIRED].GetOptionCount() != triforceTotal + 1) {
            mOptions[RSK_TRIFORCE_HUNT_PIECES_REQUIRED].ChangeOptions(NumOpts(1, triforceTotal + 1));
        }
    });
    OPT_U8(RSK_TRIFORCE_HUNT_PIECES_REQUIRED, "Triforce Hunt Required Pieces", {NumOpts(1, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("TriforceHuntRequiredPieces"), mOptionDescriptions[RSK_TRIFORCE_HUNT_PIECES_REQUIRED], WIDGET_CVAR_SLIDER_INT, 19);
    OPT_U8(RSK_TRIFORCE_HUNT_PIECES_LOCATION, "Triforce Hunt Pieces Location", {"Any Dungeon", "Overworld", "Anywhere"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("TriforceHuntPiecesLocation"), mOptionDescriptions[RSK_TRIFORCE_HUNT_PIECES_LOCATION], WIDGET_CVAR_COMBOBOX, RO_TRIFORCE_HUNT_LOCATION_ANYWHERE);
    OPT_U8(RSK_MQ_DUNGEON_RANDOM, "MQ Dungeon Setting", {"None", "Set Number", "Random", "Selection Only"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeons"), mOptionDescriptions[RSK_MQ_DUNGEON_RANDOM], WIDGET_CVAR_COMBOBOX, RO_MQ_DUNGEONS_NONE, false, nullptr, IMFLAG_NONE);
    OPT_CALLBACK(RSK_MQ_DUNGEON_RANDOM, {
        switch (CVarGetInteger(CVAR_RANDOMIZER_SETTING("MQDungeons"), RO_MQ_DUNGEONS_NONE)) {
            // If No MQ Dungeons, add a separator after the combobx and hide
            // the count slider and the toggle for individual dungeon selections.
            case RO_MQ_DUNGEONS_NONE:
                mOptions[RSK_MQ_DUNGEON_COUNT].Hide();
                mOptions[RSK_MQ_DUNGEON_SET].Hide();
                break;
            // If Set Number, remove the separator and show both the count slider and the
            // individual dungeon selection toggle.
            case RO_MQ_DUNGEONS_SET_NUMBER:
                mOptions[RSK_MQ_DUNGEON_COUNT].Unhide();
                mOptions[RSK_MQ_DUNGEON_SET].Unhide();
                break;
            // else if random number or selection only, remove the separator and only show
            // the individual dungeon selection toggle.
            case RO_MQ_DUNGEONS_RANDOM_NUMBER:
                mOptions[RSK_MQ_DUNGEON_COUNT].Hide();
                mOptions[RSK_MQ_DUNGEON_SET].Unhide();
                break;
            case RO_MQ_DUNGEONS_SELECTION:
                mOptions[RSK_MQ_DUNGEON_COUNT].Hide();
                mOptions[RSK_MQ_DUNGEON_SET].Hide();
                break;
            default:
                break;
        }
        // Controls whether or not to show the selectors for individual dungeons.
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("MQDungeons"), RO_MQ_DUNGEONS_NONE) != RO_MQ_DUNGEONS_NONE &&
            (CVarGetInteger(CVAR_RANDOMIZER_SETTING("MQDungeonsSelection"), RO_GENERIC_OFF) == RO_GENERIC_ON ||
             CVarGetInteger(CVAR_RANDOMIZER_SETTING("MQDungeons"), RO_MQ_DUNGEONS_NONE) == RO_MQ_DUNGEONS_SELECTION)) {
            // if showing the dungeon selectors, remove the separator after the Set Dungeons checkbox.
            mOptions[RSK_MQ_DEKU_TREE].Unhide();
            mOptions[RSK_MQ_DODONGOS_CAVERN].Unhide();
            mOptions[RSK_MQ_JABU_JABU].Unhide();
            mOptions[RSK_MQ_FOREST_TEMPLE].Unhide();
            mOptions[RSK_MQ_FIRE_TEMPLE].Unhide();
            mOptions[RSK_MQ_WATER_TEMPLE].Unhide();
            mOptions[RSK_MQ_SPIRIT_TEMPLE].Unhide();
            mOptions[RSK_MQ_SHADOW_TEMPLE].Unhide();
            mOptions[RSK_MQ_BOTTOM_OF_THE_WELL].Unhide();
            mOptions[RSK_MQ_ICE_CAVERN].Unhide();
            mOptions[RSK_MQ_GTG].Unhide();
            mOptions[RSK_MQ_GANONS_CASTLE].Unhide();
        } else {
            // If those are not shown, add a separator after the Set Dungeons checkbox.
            mOptions[RSK_MQ_DEKU_TREE].Hide();
            mOptions[RSK_MQ_DODONGOS_CAVERN].Hide();
            mOptions[RSK_MQ_JABU_JABU].Hide();
            mOptions[RSK_MQ_FOREST_TEMPLE].Hide();
            mOptions[RSK_MQ_FIRE_TEMPLE].Hide();
            mOptions[RSK_MQ_WATER_TEMPLE].Hide();
            mOptions[RSK_MQ_SPIRIT_TEMPLE].Hide();
            mOptions[RSK_MQ_SHADOW_TEMPLE].Hide();
            mOptions[RSK_MQ_BOTTOM_OF_THE_WELL].Hide();
            mOptions[RSK_MQ_ICE_CAVERN].Hide();
            mOptions[RSK_MQ_GTG].Hide();
            mOptions[RSK_MQ_GANONS_CASTLE].Hide();
        }
    });
    OPT_U8(RSK_MQ_DUNGEON_COUNT, "MQ Dungeon Count", {NumOpts(0, MAX_MQ_DUNGEON_COUNT)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeonCount"), "", WIDGET_CVAR_SLIDER_INT, MAX_MQ_DUNGEON_COUNT, true, nullptr, IMFLAG_NONE);
    OPT_BOOL(RSK_MQ_DUNGEON_SET, "Set Dungeon Quests", {"Off", "On"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeonsSelection"), mOptionDescriptions[RSK_MQ_DUNGEON_SET], WIDGET_CVAR_CHECKBOX, false, false, nullptr, IMFLAG_NONE);
    OPT_CALLBACK(RSK_MQ_DUNGEON_SET, {
        // Controls whether or not to show the selectors for individual dungeons.
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("MQDungeons"), RO_MQ_DUNGEONS_NONE) != RO_MQ_DUNGEONS_NONE &&
            (CVarGetInteger(CVAR_RANDOMIZER_SETTING("MQDungeonsSelection"), RO_GENERIC_OFF) == RO_GENERIC_ON ||
             CVarGetInteger(CVAR_RANDOMIZER_SETTING("MQDungeons"), RO_MQ_DUNGEONS_NONE) == RO_MQ_DUNGEONS_SELECTION)) {
            // if showing the dungeon selectors, remove the separator after the Set Dungeons checkbox.
            mOptions[RSK_MQ_DEKU_TREE].Unhide();
            mOptions[RSK_MQ_DODONGOS_CAVERN].Unhide();
            mOptions[RSK_MQ_JABU_JABU].Unhide();
            mOptions[RSK_MQ_FOREST_TEMPLE].Unhide();
            mOptions[RSK_MQ_FIRE_TEMPLE].Unhide();
            mOptions[RSK_MQ_WATER_TEMPLE].Unhide();
            mOptions[RSK_MQ_SPIRIT_TEMPLE].Unhide();
            mOptions[RSK_MQ_SHADOW_TEMPLE].Unhide();
            mOptions[RSK_MQ_BOTTOM_OF_THE_WELL].Unhide();
            mOptions[RSK_MQ_ICE_CAVERN].Unhide();
            mOptions[RSK_MQ_GTG].Unhide();
            mOptions[RSK_MQ_GANONS_CASTLE].Unhide();
        } else {
            // If those are not shown, add a separator after the Set Dungeons checkbox.
            mOptions[RSK_MQ_DEKU_TREE].Hide();
            mOptions[RSK_MQ_DODONGOS_CAVERN].Hide();
            mOptions[RSK_MQ_JABU_JABU].Hide();
            mOptions[RSK_MQ_FOREST_TEMPLE].Hide();
            mOptions[RSK_MQ_FIRE_TEMPLE].Hide();
            mOptions[RSK_MQ_WATER_TEMPLE].Hide();
            mOptions[RSK_MQ_SPIRIT_TEMPLE].Hide();
            mOptions[RSK_MQ_SHADOW_TEMPLE].Hide();
            mOptions[RSK_MQ_BOTTOM_OF_THE_WELL].Hide();
            mOptions[RSK_MQ_ICE_CAVERN].Hide();
            mOptions[RSK_MQ_GTG].Hide();
            mOptions[RSK_MQ_GANONS_CASTLE].Hide();
        }
    });
    OPT_U8(RSK_MQ_DEKU_TREE, "Deku Tree Quest", {"Vanilla", "Master Quest", "Random"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeonsDekuTree"), "", WIDGET_CVAR_COMBOBOX, RO_MQ_SET_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MQ_DODONGOS_CAVERN, "Dodongo's Cavern Quest", {"Vanilla", "Master Quest", "Random"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeonsDodongosCavern"), "", WIDGET_CVAR_COMBOBOX, RO_MQ_SET_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MQ_JABU_JABU, "Jabu-Jabu's Belly Quest", {"Vanilla", "Master Quest", "Random"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeonsJabuJabu"), "", WIDGET_CVAR_COMBOBOX, RO_MQ_SET_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MQ_FOREST_TEMPLE, "Forest Temple Quest", {"Vanilla", "Master Quest", "Random"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeonsForestTemple"), "", WIDGET_CVAR_COMBOBOX, RO_MQ_SET_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MQ_FIRE_TEMPLE, "Fire Temple Quest", {"Vanilla", "Master Quest", "Random"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeonsFireTemple"), "", WIDGET_CVAR_COMBOBOX, RO_MQ_SET_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MQ_WATER_TEMPLE, "Water Temple Quest", {"Vanilla", "Master Quest", "Random"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeonsWaterTemple"), "", WIDGET_CVAR_COMBOBOX, RO_MQ_SET_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MQ_SPIRIT_TEMPLE, "Spirit Temple Quest", {"Vanilla", "Master Quest", "Random"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeonsSpiritTemple"), "", WIDGET_CVAR_COMBOBOX, RO_MQ_SET_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MQ_SHADOW_TEMPLE, "Shadow Temple Quest", {"Vanilla", "Master Quest", "Random"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeonsShadowTemple"), "", WIDGET_CVAR_COMBOBOX, RO_MQ_SET_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MQ_BOTTOM_OF_THE_WELL, "Bottom of the Well Quest", {"Vanilla", "Master Quest", "Random"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeonsBottomOfTheWell"), "", WIDGET_CVAR_COMBOBOX, RO_MQ_SET_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MQ_ICE_CAVERN, "Ice Cavern Quest", {"Vanilla", "Master Quest", "Random"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeonsIceCavern"), "", WIDGET_CVAR_COMBOBOX, RO_MQ_SET_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MQ_GTG, "Gerudo Training Ground Quest", {"Vanilla", "Master Quest", "Random"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeonsGTG"), "", WIDGET_CVAR_COMBOBOX, RO_MQ_SET_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MQ_GANONS_CASTLE, "Ganon's Castle Quest", {"Vanilla", "Master Quest", "Random"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MQDungeonsGanonsCastle"), "", WIDGET_CVAR_COMBOBOX, RO_MQ_SET_VANILLA);
    OPT_U8(RSK_SHUFFLE_DUNGEON_REWARDS, "Shuffle Dungeon Rewards", {"Vanilla", "End of Dungeons", "Own Dungeon", "Any Dungeon", "Overworld", "Anywhere"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleDungeonReward"), mOptionDescriptions[RSK_SHUFFLE_DUNGEON_REWARDS], WIDGET_CVAR_COMBOBOX, RO_DUNGEON_REWARDS_END_OF_DUNGEON);
    OPT_CALLBACK(RSK_SHUFFLE_DUNGEON_REWARDS, {
        // Link's Pocket - Disabled when Dungeon Rewards are shuffled to End of Dungeon
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleDungeonReward"), RO_DUNGEON_REWARDS_END_OF_DUNGEON) ==
            RO_DUNGEON_REWARDS_END_OF_DUNGEON) {
            mOptions[RSK_LINKS_POCKET].Disable(
                "This option is disabled because \"Dungeon Rewards\" are shuffled to \"End of Dungeons\".");
            mOptions[RSK_LINKS_POCKET_REWARD].Enable();
            mOptions[RSK_LINKS_POCKET_REWARD].Unhide();
        } else {
            if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleDungeonReward"), RO_DUNGEON_REWARDS_END_OF_DUNGEON) ==
                RO_DUNGEON_REWARDS_OWN_DUNGEON) {
                mOptions[RSK_LINKS_POCKET].Enable();
                mOptions[RSK_LINKS_POCKET_REWARD].Disable(
                    "As \"Link's Pocket\" is set to \"Dungeon Reward\" while \"Dungeon Rewards\" is set to \"Own Dungeon\", Link's Pocket will always have the Light Medallion");
            }else if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleDungeonReward"), RO_DUNGEON_REWARDS_END_OF_DUNGEON) ==
                RO_DUNGEON_REWARDS_VANILLA) {
                mOptions[RSK_LINKS_POCKET].Enable();
                mOptions[RSK_LINKS_POCKET_REWARD].Disable(
                    "As \"Link's Pocket\" is set to \"Dungeon Reward\" while \"Dungeon Rewards\" is set to \"Vanilla\", Link's Pocket will always have the Light Medallion");
            } else {
                mOptions[RSK_LINKS_POCKET].Enable();
                mOptions[RSK_LINKS_POCKET_REWARD].Enable();
            }
            if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("LinksPocket"), RO_LINKS_POCKET_DUNGEON_REWARD) == RO_LINKS_POCKET_DUNGEON_REWARD) {
                mOptions[RSK_LINKS_POCKET_REWARD].Unhide();
            } else {
                mOptions[RSK_LINKS_POCKET_REWARD].Hide();
            }
        }
    });
    OPT_U8(RSK_LINKS_POCKET, "Link's Pocket", {"Dungeon Reward", "Advancement", "Anything", "Nothing"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("LinksPocket"), mOptionDescriptions[RSK_LINKS_POCKET], WIDGET_CVAR_COMBOBOX, RO_LINKS_POCKET_DUNGEON_REWARD);
    OPT_CALLBACK(RSK_LINKS_POCKET, {
        // Only show the dungeon reward type if Link's Pocket is set to Dungeon Reward and Dungeon Rewards are not Vanilla, OR Dungeon Rewards are end of dungeon
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("LinksPocket"), RO_LINKS_POCKET_DUNGEON_REWARD) == RO_LINKS_POCKET_DUNGEON_REWARD ||
            CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleDungeonReward"), RO_DUNGEON_REWARDS_END_OF_DUNGEON) == RO_DUNGEON_REWARDS_END_OF_DUNGEON) {
            mOptions[RSK_LINKS_POCKET_REWARD].Unhide();
        } else {
            mOptions[RSK_LINKS_POCKET_REWARD].Hide();
        }
    });
    OPT_U8(RSK_LINKS_POCKET_REWARD, "Link's Pocket Reward Type", {"Any Reward", "Any Stone", "Any Medallion", "Light Medallion"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("LinksPocketReward"), mOptionDescriptions[RSK_LINKS_POCKET_REWARD], WIDGET_CVAR_COMBOBOX, RO_LINKS_POCKET_ANY_REWARD);
    OPT_U8(RSK_SHUFFLE_SONGS, "Shuffle Songs", {"Off", "Song Locations", "Dungeon Rewards", "Anywhere"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleSongs"), mOptionDescriptions[RSK_SHUFFLE_SONGS], WIDGET_CVAR_COMBOBOX, RO_SONG_SHUFFLE_SONG_LOCATIONS);
    OPT_U8(RSK_SHOPSANITY, "Shop Shuffle", {"Off", "Specific Count", "Random"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("Shopsanity"), mOptionDescriptions[RSK_SHOPSANITY], WIDGET_CVAR_COMBOBOX, RO_SHOPSANITY_OFF);
    OPT_CALLBACK(RSK_SHOPSANITY, {
        // Hide shopsanity prices if shopsanity is off or zero
        switch (CVarGetInteger(CVAR_RANDOMIZER_SETTING("Shopsanity"), RO_SHOPSANITY_OFF)) {
            case RO_SHOPSANITY_OFF:
                mOptions[RSK_SHOPSANITY].AddFlag(IMFLAG_SEPARATOR_BOTTOM);
                mOptions[RSK_SHOPSANITY_COUNT].Hide();
                mOptions[RSK_SHOPSANITY_COUNT].Hide();
                mOptions[RSK_SHOPSANITY_PRICES].Hide();
                mOptions[RSK_SHOPSANITY_PRICES_AFFORDABLE].Hide();
                mOptions[RSK_SHOPSANITY_PRICES_FIXED_PRICE].Hide();
                mOptions[RSK_SHOPSANITY_PRICES_RANGE_1].Hide();
                mOptions[RSK_SHOPSANITY_PRICES_RANGE_2].Hide();
                mOptions[RSK_SHOPSANITY_PRICES_NO_WALLET_WEIGHT].Hide();
                mOptions[RSK_SHOPSANITY_PRICES_CHILD_WALLET_WEIGHT].Hide();
                mOptions[RSK_SHOPSANITY_PRICES_ADULT_WALLET_WEIGHT].Hide();
                mOptions[RSK_SHOPSANITY_PRICES_GIANT_WALLET_WEIGHT].Hide();
                mOptions[RSK_SHOPSANITY_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                break;
            case RO_SHOPSANITY_SPECIFIC_COUNT:
                mOptions[RSK_SHOPSANITY_COUNT].Unhide();
                HandleShopsanityPriceUI();
                break;
            case RO_SHOPSANITY_RANDOM:
                mOptions[RSK_SHOPSANITY_COUNT].Hide();
                HandleShopsanityPriceUI();
                break;
        }
    });
    OPT_U8(RSK_SHOPSANITY_COUNT, "Shops Item Count", {NumOpts(0, 8)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShopsanityCount"), mOptionDescriptions[RSK_SHOPSANITY_COUNT], WIDGET_CVAR_SLIDER_INT, 0, false, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_SHOPSANITY_PRICES, "Shops Prices", {"Vanilla", "Cheap Balanced", "Balanced", "Fixed", "Range", "Set By Wallet"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShopsanityPrices"), mOptionDescriptions[RSK_SHOPSANITY_PRICES], WIDGET_CVAR_COMBOBOX, RO_PRICE_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_CALLBACK(RSK_SHOPSANITY_PRICES, {
        HandleShopsanityPriceUI();
    });
    OPT_U8(RSK_SHOPSANITY_PRICES_FIXED_PRICE, "Shops Fixed Price", {NumOpts(0, 995, 5)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShopsanityFixedPrice"), mOptionDescriptions[RSK_SHOPSANITY_PRICES_FIXED_PRICE], WIDGET_CVAR_SLIDER_INT, 10, true);
    OPT_U8(RSK_SHOPSANITY_PRICES_RANGE_1, "Shops Lower Bound", {NumOpts(0, 995, 5)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShopsanityPriceRange1"), mOptionDescriptions[RSK_SHOPSANITY_PRICES_RANGE_1], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_SHOPSANITY_PRICES_RANGE_2, "Shops Upper Bound", {NumOpts(0, 995, 5)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShopsanityPriceRange2"), mOptionDescriptions[RSK_SHOPSANITY_PRICES_RANGE_2], WIDGET_CVAR_SLIDER_INT, 100, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_SHOPSANITY_PRICES_NO_WALLET_WEIGHT, "Shops No Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShopsanityNoWalletWeight"), mOptionDescriptions[RSK_SHOPSANITY_PRICES_NO_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_SHOPSANITY_PRICES_CHILD_WALLET_WEIGHT, "Shops Child Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShopsanityChildWalletWeight"), mOptionDescriptions[RSK_SHOPSANITY_PRICES_CHILD_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_SHOPSANITY_PRICES_ADULT_WALLET_WEIGHT, "Shops Adult Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShopsanityAdultWalletWeight"), mOptionDescriptions[RSK_SHOPSANITY_PRICES_ADULT_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_SHOPSANITY_PRICES_GIANT_WALLET_WEIGHT, "Shops Giant Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShopsanityGiantWalletWeight"), mOptionDescriptions[RSK_SHOPSANITY_PRICES_GIANT_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_SHOPSANITY_PRICES_TYCOON_WALLET_WEIGHT, "Shops Tycoon Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShopsanityTycoonWalletWeight"), mOptionDescriptions[RSK_SHOPSANITY_PRICES_TYCOON_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_BOOL(RSK_SHOPSANITY_PRICES_AFFORDABLE, "Shops Affordable Prices", CVAR_RANDOMIZER_SETTING("ShopsanityPricesAffordable"), mOptionDescriptions[RSK_SHOPSANITY_PRICES_AFFORDABLE]);
    OPT_BOOL(RSK_SHOP_SHIELDS_AND_TUNICS_ONLY_REFILL, "Gate Shop Shields & Tunics", CVAR_RANDOMIZER_SETTING("ShopShieldsTunicsGate"), mOptionDescriptions[RSK_SHOP_SHIELDS_AND_TUNICS_ONLY_REFILL]);
    OPT_U8(RSK_SHUFFLE_TOKENS, "Token Shuffle", {"Off", "Dungeons", "Overworld", "All Tokens"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleTokens"), mOptionDescriptions[RSK_SHUFFLE_TOKENS], WIDGET_CVAR_COMBOBOX, RO_TOKENSANITY_OFF);
    OPT_U8(RSK_SHUFFLE_SCRUBS, "Scrubs Shuffle", {"Off", "One-Time Only", "All"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleScrubs"), mOptionDescriptions[RSK_SHUFFLE_SCRUBS], WIDGET_CVAR_COMBOBOX, RO_SCRUBS_OFF);
    OPT_CALLBACK(RSK_SHUFFLE_SCRUBS, {
        bool isTycoon = CVarGetInteger(CVAR_RANDOMIZER_SETTING("IncludeTycoonWallet"), RO_GENERIC_OFF);
        switch (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleScrubs"), RO_SCRUBS_OFF)) {
            case RO_SCRUBS_OFF:
                mOptions[RSK_SCRUBS_PRICES].Hide();
                mOptions[RSK_SCRUBS_PRICES_AFFORDABLE].Hide();
                mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].Hide();
                mOptions[RSK_SCRUBS_PRICES_RANGE_1].Hide();
                mOptions[RSK_SCRUBS_PRICES_RANGE_2].Hide();
                mOptions[RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                break;
            default:
                mOptions[RSK_SCRUBS_PRICES].Unhide();
                switch (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ScrubsPrices"), RO_PRICE_VANILLA)) {
                    case RO_PRICE_FIXED:
                        mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].Unhide();
                        mOptions[RSK_SCRUBS_PRICES_RANGE_1].Hide();
                        mOptions[RSK_SCRUBS_PRICES_RANGE_2].Hide();
                        mOptions[RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT].Hide();
                        mOptions[RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT].Hide();
                        mOptions[RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT].Hide();
                        mOptions[RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT].Hide();
                        mOptions[RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                        if (isTycoon ? mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].GetOptionCount() == 501
                                    : mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].GetOptionCount() == 1000) {
                            mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].ChangeOptions(isTycoon ? NumOpts(0, 999)
                                                                                        : NumOpts(0, 500));
                        }
                        mOptions[RSK_SCRUBS_PRICES_AFFORDABLE].Hide();
                        break;
                    case RO_PRICE_RANGE:
                        mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].Hide();
                        mOptions[RSK_SCRUBS_PRICES_RANGE_1].Unhide();
                        mOptions[RSK_SCRUBS_PRICES_RANGE_2].Unhide();
                        mOptions[RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT].Hide();
                        mOptions[RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT].Hide();
                        mOptions[RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT].Hide();
                        mOptions[RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT].Hide();
                        mOptions[RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                        if (isTycoon ? mOptions[RSK_SCRUBS_PRICES_RANGE_1].GetOptionCount() == 101
                                    : mOptions[RSK_SCRUBS_PRICES_RANGE_1].GetOptionCount() == 200) {
                            mOptions[RSK_SCRUBS_PRICES_RANGE_1].ChangeOptions(isTycoon ? NumOpts(0, 995, 5)
                                                                                    : NumOpts(0, 500, 5));
                            mOptions[RSK_SCRUBS_PRICES_RANGE_2].ChangeOptions(isTycoon ? NumOpts(0, 995, 5)
                                                                                    : NumOpts(0, 500, 5));
                        }
                        mOptions[RSK_SCRUBS_PRICES_AFFORDABLE].Unhide();
                        break;
                    case RO_PRICE_SET_BY_WALLET:
                        mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].Hide();
                        mOptions[RSK_SCRUBS_PRICES_RANGE_1].Hide();
                        mOptions[RSK_SCRUBS_PRICES_RANGE_2].Hide();
                        mOptions[RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT].Unhide();
                        mOptions[RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT].Unhide();
                        mOptions[RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT].Unhide();
                        mOptions[RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT].Unhide();
                        if (isTycoon) {
                            mOptions[RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT].Unhide();
                        } else {
                            mOptions[RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                        }
                        mOptions[RSK_SCRUBS_PRICES_AFFORDABLE].Unhide();
                        break;
                    default:
                        mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].Hide();
                        mOptions[RSK_SCRUBS_PRICES_RANGE_1].Hide();
                        mOptions[RSK_SCRUBS_PRICES_RANGE_2].Hide();
                        mOptions[RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT].Hide();
                        mOptions[RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT].Hide();
                        mOptions[RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT].Hide();
                        mOptions[RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT].Hide();
                        mOptions[RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                        mOptions[RSK_SCRUBS_PRICES_AFFORDABLE].Unhide();
                        break;
                }
                break;
        }
    });
    OPT_U8(RSK_SCRUBS_PRICES, "Scrubs Prices", {"Vanilla", "Cheap Balanced", "Balanced", "Fixed", "Range", "Set By Wallet"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ScrubsPrices"), mOptionDescriptions[RSK_SCRUBS_PRICES], WIDGET_CVAR_COMBOBOX, RO_PRICE_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_CALLBACK(RSK_SCRUBS_PRICES, {
        bool isTycoon = CVarGetInteger(CVAR_RANDOMIZER_SETTING("IncludeTycoonWallet"), RO_GENERIC_OFF);
        switch (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ScrubsPrices"), RO_PRICE_VANILLA)) {
            case RO_PRICE_FIXED:
                mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].Unhide();
                mOptions[RSK_SCRUBS_PRICES_RANGE_1].Hide();
                mOptions[RSK_SCRUBS_PRICES_RANGE_2].Hide();
                mOptions[RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                if (isTycoon ? mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].GetOptionCount() == 501
                            : mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].GetOptionCount() == 1000) {
                    mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].ChangeOptions(isTycoon ? NumOpts(0, 999)
                                                                                : NumOpts(0, 500));
                }
                mOptions[RSK_SCRUBS_PRICES_AFFORDABLE].Hide();
                break;
            case RO_PRICE_RANGE:
                mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].Hide();
                mOptions[RSK_SCRUBS_PRICES_RANGE_1].Unhide();
                mOptions[RSK_SCRUBS_PRICES_RANGE_2].Unhide();
                mOptions[RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                if (isTycoon ? mOptions[RSK_SCRUBS_PRICES_RANGE_1].GetOptionCount() == 101
                            : mOptions[RSK_SCRUBS_PRICES_RANGE_1].GetOptionCount() == 200) {
                    mOptions[RSK_SCRUBS_PRICES_RANGE_1].ChangeOptions(isTycoon ? NumOpts(0, 995, 5)
                                                                            : NumOpts(0, 500, 5));
                    mOptions[RSK_SCRUBS_PRICES_RANGE_2].ChangeOptions(isTycoon ? NumOpts(0, 995, 5)
                                                                            : NumOpts(0, 500, 5));
                }
                mOptions[RSK_SCRUBS_PRICES_AFFORDABLE].Unhide();
                break;
            case RO_PRICE_SET_BY_WALLET:
                mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].Hide();
                mOptions[RSK_SCRUBS_PRICES_RANGE_1].Hide();
                mOptions[RSK_SCRUBS_PRICES_RANGE_2].Hide();
                mOptions[RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT].Unhide();
                mOptions[RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT].Unhide();
                mOptions[RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT].Unhide();
                mOptions[RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT].Unhide();
                if (isTycoon) {
                    mOptions[RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT].Unhide();
                } else {
                    mOptions[RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                }
                mOptions[RSK_SCRUBS_PRICES_AFFORDABLE].Unhide();
                break;
            default:
                mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE].Hide();
                mOptions[RSK_SCRUBS_PRICES_RANGE_1].Hide();
                mOptions[RSK_SCRUBS_PRICES_RANGE_2].Hide();
                mOptions[RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                mOptions[RSK_SCRUBS_PRICES_AFFORDABLE].Unhide();
                break;
        }
    });
    OPT_U8(RSK_SCRUBS_PRICES_FIXED_PRICE, "Scrubs Fixed Price", {NumOpts(0, 995, 5)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ScrubsFixedPrice"), mOptionDescriptions[RSK_SCRUBS_PRICES_FIXED_PRICE], WIDGET_CVAR_SLIDER_INT, 10, true);
    OPT_U8(RSK_SCRUBS_PRICES_RANGE_1, "Scrubs Lower Bound", {NumOpts(0, 995, 5)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ScrubsPriceRange1"), mOptionDescriptions[RSK_SCRUBS_PRICES_RANGE_1], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_SCRUBS_PRICES_RANGE_2, "Scrubs Upper Bound", {NumOpts(0, 995, 5)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ScrubsPriceRange2"), mOptionDescriptions[RSK_SCRUBS_PRICES_RANGE_2], WIDGET_CVAR_SLIDER_INT, 100, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT, "Scrubs No Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ScrubsNoWalletWeight"), mOptionDescriptions[RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT, "Scrubs Child Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ScrubsChildWalletWeight"), mOptionDescriptions[RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT, "Scrubs Adult Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ScrubsAdultWalletWeight"), mOptionDescriptions[RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT, "Scrubs Giant Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ScrubsGiantWalletWeight"), mOptionDescriptions[RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT, "Scrubs Tycoon Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ScrubsTycoonWalletWeight"), mOptionDescriptions[RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_BOOL(RSK_SCRUBS_PRICES_AFFORDABLE, "Scrubs Affordable Prices", CVAR_RANDOMIZER_SETTING("ScrubsPricesAffordable"), mOptionDescriptions[RSK_SCRUBS_PRICES_AFFORDABLE]);
    OPT_BOOL(RSK_SHUFFLE_BEEHIVES, "Shuffle Beehives", CVAR_RANDOMIZER_SETTING("ShuffleBeehives"), mOptionDescriptions[RSK_SHUFFLE_BEEHIVES]);
    OPT_CALLBACK(RSK_SHUFFLE_BEEHIVES, {
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleBeehives"), RO_GENERIC_OFF)) {
            mOptions[RSK_SLINGBOW_BREAK_BEEHIVES].Enable();
        } else {
            mOptions[RSK_SLINGBOW_BREAK_BEEHIVES].Disable(
                "This option is disabled because Shuffle Beehives is not enabled.");
        }
    });
    OPT_BOOL(RSK_SHUFFLE_COWS, "Shuffle Cows", CVAR_RANDOMIZER_SETTING("ShuffleCows"), mOptionDescriptions[RSK_SHUFFLE_COWS]);
    OPT_CALLBACK(RSK_SHUFFLE_COWS, {
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleCows"), RO_GENERIC_OFF)) {
            mOptions[RSK_MALON_HINT].Enable();
        } else {
            mOptions[RSK_MALON_HINT].Disable("Malon's hint points to a cow, so requires cows to be shuffled.");
        }
    });
    OPT_BOOL(RSK_SHUFFLE_KOKIRI_SWORD, "Shuffle Kokiri Sword", CVAR_RANDOMIZER_SETTING("ShuffleKokiriSword"), mOptionDescriptions[RSK_SHUFFLE_KOKIRI_SWORD]);
    OPT_BOOL(RSK_SHUFFLE_MASTER_SWORD, "Shuffle Master Sword", CVAR_RANDOMIZER_SETTING("ShuffleMasterSword"), mOptionDescriptions[RSK_SHUFFLE_MASTER_SWORD]);
    OPT_BOOL(RSK_SWORDLESS_EPONA_ITEMS, "Swordless Epona Items", CVAR_RANDOMIZER_SETTING("SwordlessEponaItems"), mOptionDescriptions[RSK_SWORDLESS_EPONA_ITEMS]);
    OPT_BOOL(RSK_SHUFFLE_CHILD_WALLET, "Shuffle Child's Wallet", CVAR_RANDOMIZER_SETTING("ShuffleChildWallet"), mOptionDescriptions[RSK_SHUFFLE_CHILD_WALLET], IMFLAG_NONE);
    OPT_BOOL(RSK_INCLUDE_TYCOON_WALLET, "Include Tycoon Wallet", CVAR_RANDOMIZER_SETTING("IncludeTycoonWallet"), mOptionDescriptions[RSK_INCLUDE_TYCOON_WALLET]);
    OPT_BOOL(RSK_SHUFFLE_OCARINA, "Shuffle Ocarinas", CVAR_RANDOMIZER_SETTING("ShuffleOcarinas"), mOptionDescriptions[RSK_SHUFFLE_OCARINA]);
    OPT_CALLBACK(RSK_SHUFFLE_OCARINA, {
        HandleStartingAgeUI();
    });
    OPT_BOOL(RSK_SHUFFLE_OCARINA_BUTTONS, "Shuffle Ocarina Buttons", CVAR_RANDOMIZER_SETTING("ShuffleOcarinaButtons"), mOptionDescriptions[RSK_SHUFFLE_OCARINA_BUTTONS]);
    OPT_BOOL(RSK_SHUFFLE_SWIM, "Shuffle Swim", CVAR_RANDOMIZER_SETTING("ShuffleSwim"), mOptionDescriptions[RSK_SHUFFLE_SWIM]);
    OPT_BOOL(RSK_SHUFFLE_CLIMB, "Shuffle Climb", CVAR_RANDOMIZER_SETTING("ShuffleClimb"), mOptionDescriptions[RSK_SHUFFLE_CLIMB]);
    OPT_BOOL(RSK_SHUFFLE_CRAWL, "Shuffle Crawl", CVAR_RANDOMIZER_SETTING("ShuffleCrawl"), mOptionDescriptions[RSK_SHUFFLE_CRAWL]);
    OPT_BOOL(RSK_SHUFFLE_GRAB, "Shuffle Grab", CVAR_RANDOMIZER_SETTING("ShuffleGrab"), mOptionDescriptions[RSK_SHUFFLE_GRAB]);
    OPT_BOOL(RSK_SHUFFLE_SPEAK, "Shuffle Jabber Nuts", CVAR_RANDOMIZER_SETTING("ShuffleSpeak"), mOptionDescriptions[RSK_SHUFFLE_SPEAK]);
    OPT_BOOL(RSK_SHUFFLE_OPEN_CHEST, "Shuffle Open Chest", CVAR_RANDOMIZER_SETTING("ShuffleOpenChest"), mOptionDescriptions[RSK_SHUFFLE_OPEN_CHEST]);
    OPT_BOOL(RSK_SHUFFLE_WEIRD_EGG, "Shuffle Weird Egg", CVAR_RANDOMIZER_SETTING("ShuffleWeirdEgg"), mOptionDescriptions[RSK_SHUFFLE_WEIRD_EGG]);
    OPT_BOOL(RSK_SHUFFLE_GERUDO_MEMBERSHIP_CARD, "Shuffle Gerudo Membership Card", CVAR_RANDOMIZER_SETTING("ShuffleGerudoToken"), mOptionDescriptions[RSK_SHUFFLE_GERUDO_MEMBERSHIP_CARD]);
    OPT_U8(RSK_SHUFFLE_POTS, "Shuffle Pots", {"Off", "Dungeons", "Overworld", "All Pots"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShufflePots"), mOptionDescriptions[RSK_SHUFFLE_POTS], WIDGET_CVAR_COMBOBOX, RO_SHUFFLE_POTS_OFF);
    OPT_U8(RSK_SHUFFLE_GRASS, "Shuffle Grass", {"Off", "Dungeons", "Overworld", "All Grass"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleGrass"), mOptionDescriptions[RSK_SHUFFLE_GRASS], WIDGET_CVAR_COMBOBOX, RO_SHUFFLE_GRASS_OFF);
    OPT_U8(RSK_SHUFFLE_CRATES, "Shuffle Crates", {"Off", "Dungeons", "Overworld", "All Crates"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleCrates"), mOptionDescriptions[RSK_SHUFFLE_CRATES], WIDGET_CVAR_COMBOBOX, RO_SHUFFLE_CRATES_OFF);
    OPT_BOOL(RSK_SHUFFLE_ROCKS, "Shuffle Rocks", CVAR_RANDOMIZER_SETTING("ShuffleRocks"), mOptionDescriptions[RSK_SHUFFLE_ROCKS]);
    OPT_U8(RSK_SHUFFLE_BOULDERS, "Shuffle Boulders", {"Off", "Dungeons", "Overworld", "All Boulders"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleBoulders"), mOptionDescriptions[RSK_SHUFFLE_BOULDERS], WIDGET_CVAR_COMBOBOX, RO_SHUFFLE_BOULDERS_OFF);
    OPT_BOOL(RSK_SHUFFLE_TREES, "Shuffle Trees", CVAR_RANDOMIZER_SETTING("ShuffleTrees"), mOptionDescriptions[RSK_SHUFFLE_TREES]);
    OPT_BOOL(RSK_SHUFFLE_BUSHES, "Shuffle Bushes", CVAR_RANDOMIZER_SETTING("ShuffleBushes"), mOptionDescriptions[RSK_SHUFFLE_BUSHES]);
    OPT_BOOL(RSK_SHUFFLE_ICICLES, "Shuffle Icicles", CVAR_RANDOMIZER_SETTING("ShuffleIcicles"), mOptionDescriptions[RSK_SHUFFLE_ICICLES]);
    OPT_BOOL(RSK_SHUFFLE_RED_ICE, "Shuffle Red Ice", CVAR_RANDOMIZER_SETTING("ShuffleRedIce"), mOptionDescriptions[RSK_SHUFFLE_RED_ICE]);
    OPT_U8(RSK_SHUFFLE_SIGNS, "Shuffle Signs", {"Off", "Dungeons", "Overworld", "All Signs"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleSigns"), mOptionDescriptions[RSK_SHUFFLE_SIGNS], WIDGET_CVAR_COMBOBOX, RO_SHUFFLE_SIGNS_OFF);
    OPT_BOOL(RSK_SHUFFLE_FISHING_POLE, "Shuffle Fishing Pole", CVAR_RANDOMIZER_SETTING("ShuffleFishingPole"), mOptionDescriptions[RSK_SHUFFLE_FISHING_POLE]);
    OPT_CALLBACK(RSK_SHUFFLE_FISHING_POLE, {
        // Disable fishing pole hint if the fishing pole is not shuffled
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleFishingPole"), RO_GENERIC_OFF)) {
            mOptions[RSK_FISHING_POLE_HINT].Enable();
        } else {
            mOptions[RSK_FISHING_POLE_HINT].Disable("This option is disabled since the fishing pole is not shuffled.");
        }
    });
    OPT_U8(RSK_SHUFFLE_MERCHANTS, "Shuffle Merchants", {"Off", "Bean Merchant Only", "All But Beans", "All"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleMerchants"), mOptionDescriptions[RSK_SHUFFLE_MERCHANTS], WIDGET_CVAR_COMBOBOX, RO_SHUFFLE_MERCHANTS_OFF, IMFLAG_NONE);
    OPT_CALLBACK(RSK_SHUFFLE_MERCHANTS, {
        bool isTycoon = CVarGetInteger(CVAR_RANDOMIZER_SETTING("IncludeTycoonWallet"), RO_GENERIC_OFF);
        switch (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleMerchants"), RO_SHUFFLE_MERCHANTS_OFF)) {
            case RO_SHUFFLE_MERCHANTS_OFF:
                mOptions[RSK_MERCHANT_PRICES].Hide();
                mOptions[RSK_MERCHANT_PRICES_AFFORDABLE].Hide();
                mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].Hide();
                mOptions[RSK_MERCHANT_PRICES_RANGE_1].Hide();
                mOptions[RSK_MERCHANT_PRICES_RANGE_2].Hide();
                mOptions[RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                break;
            default:
                mOptions[RSK_MERCHANT_PRICES].Unhide();
                switch (CVarGetInteger(CVAR_RANDOMIZER_SETTING("MerchantPrices"), RO_PRICE_VANILLA)) {
                    case RO_PRICE_FIXED:
                        mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].Unhide();
                        mOptions[RSK_MERCHANT_PRICES_RANGE_1].Hide();
                        mOptions[RSK_MERCHANT_PRICES_RANGE_2].Hide();
                        mOptions[RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT].Hide();
                        mOptions[RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT].Hide();
                        mOptions[RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT].Hide();
                        mOptions[RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT].Hide();
                        mOptions[RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                        if (isTycoon ? mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].GetOptionCount() == 501
                                    : mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].GetOptionCount() == 1000) {
                            mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].ChangeOptions(isTycoon ? NumOpts(0, 999)
                                                                                            : NumOpts(0, 500));
                        }
                        mOptions[RSK_MERCHANT_PRICES_AFFORDABLE].Hide();
                        break;
                    case RO_PRICE_RANGE:
                        mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].Hide();
                        mOptions[RSK_MERCHANT_PRICES_RANGE_1].Unhide();
                        mOptions[RSK_MERCHANT_PRICES_RANGE_2].Unhide();
                        mOptions[RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT].Hide();
                        mOptions[RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT].Hide();
                        mOptions[RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT].Hide();
                        mOptions[RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT].Hide();
                        mOptions[RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                        if (isTycoon ? mOptions[RSK_MERCHANT_PRICES_RANGE_1].GetOptionCount() == 101
                                    : mOptions[RSK_MERCHANT_PRICES_RANGE_1].GetOptionCount() == 200) {
                            mOptions[RSK_MERCHANT_PRICES_RANGE_1].ChangeOptions(isTycoon ? NumOpts(0, 995, 5)
                                                                                        : NumOpts(0, 500, 5));
                            mOptions[RSK_MERCHANT_PRICES_RANGE_2].ChangeOptions(isTycoon ? NumOpts(0, 995, 5)
                                                                                        : NumOpts(0, 500, 5));
                        }
                        mOptions[RSK_MERCHANT_PRICES_AFFORDABLE].Unhide();
                        break;
                    case RO_PRICE_SET_BY_WALLET:
                        mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].Hide();
                        mOptions[RSK_MERCHANT_PRICES_RANGE_1].Hide();
                        mOptions[RSK_MERCHANT_PRICES_RANGE_2].Hide();
                        mOptions[RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT].Unhide();
                        mOptions[RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT].Unhide();
                        mOptions[RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT].Unhide();
                        mOptions[RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT].Unhide();
                        if (isTycoon) {
                            mOptions[RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT].Unhide();
                        } else {
                            mOptions[RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                        }
                        mOptions[RSK_MERCHANT_PRICES_AFFORDABLE].Unhide();
                        break;
                    default:
                        mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].Hide();
                        mOptions[RSK_MERCHANT_PRICES_RANGE_1].Hide();
                        mOptions[RSK_MERCHANT_PRICES_RANGE_2].Hide();
                        mOptions[RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT].Hide();
                        mOptions[RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT].Hide();
                        mOptions[RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT].Hide();
                        mOptions[RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT].Hide();
                        mOptions[RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                        mOptions[RSK_MERCHANT_PRICES_AFFORDABLE].Unhide();
                        break;
                }
                break;
        }
    });
    OPT_U8(RSK_MERCHANT_PRICES, "Merchant Prices", {"Vanilla", "Cheap Balanced", "Balanced", "Fixed", "Range", "Set By Wallet"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MerchantPrices"), mOptionDescriptions[RSK_MERCHANT_PRICES], WIDGET_CVAR_COMBOBOX, RO_PRICE_VANILLA, false, nullptr, IMFLAG_NONE);
    OPT_CALLBACK(RSK_MERCHANT_PRICES, {
        bool isTycoon = CVarGetInteger(CVAR_RANDOMIZER_SETTING("IncludeTycoonWallet"), RO_GENERIC_OFF);
        switch (CVarGetInteger(CVAR_RANDOMIZER_SETTING("MerchantPrices"), RO_PRICE_VANILLA)) {
            case RO_PRICE_FIXED:
                mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].Unhide();
                mOptions[RSK_MERCHANT_PRICES_RANGE_1].Hide();
                mOptions[RSK_MERCHANT_PRICES_RANGE_2].Hide();
                mOptions[RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                if (isTycoon ? mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].GetOptionCount() == 501
                            : mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].GetOptionCount() == 1000) {
                    mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].ChangeOptions(isTycoon ? NumOpts(0, 999)
                                                                                    : NumOpts(0, 500));
                }
                mOptions[RSK_MERCHANT_PRICES_AFFORDABLE].Hide();
                break;
            case RO_PRICE_RANGE:
                mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].Hide();
                mOptions[RSK_MERCHANT_PRICES_RANGE_1].Unhide();
                mOptions[RSK_MERCHANT_PRICES_RANGE_2].Unhide();
                mOptions[RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                if (isTycoon ? mOptions[RSK_MERCHANT_PRICES_RANGE_1].GetOptionCount() == 101
                            : mOptions[RSK_MERCHANT_PRICES_RANGE_1].GetOptionCount() == 200) {
                    mOptions[RSK_MERCHANT_PRICES_RANGE_1].ChangeOptions(isTycoon ? NumOpts(0, 995, 5)
                                                                                : NumOpts(0, 500, 5));
                    mOptions[RSK_MERCHANT_PRICES_RANGE_2].ChangeOptions(isTycoon ? NumOpts(0, 995, 5)
                                                                                : NumOpts(0, 500, 5));
                }
                mOptions[RSK_MERCHANT_PRICES_AFFORDABLE].Unhide();
                break;
            case RO_PRICE_SET_BY_WALLET:
                mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].Hide();
                mOptions[RSK_MERCHANT_PRICES_RANGE_1].Hide();
                mOptions[RSK_MERCHANT_PRICES_RANGE_2].Hide();
                mOptions[RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT].Unhide();
                mOptions[RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT].Unhide();
                mOptions[RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT].Unhide();
                mOptions[RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT].Unhide();
                if (isTycoon) {
                    mOptions[RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT].Unhide();
                } else {
                    mOptions[RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                }
                mOptions[RSK_MERCHANT_PRICES_AFFORDABLE].Unhide();
                break;
            default:
                mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE].Hide();
                mOptions[RSK_MERCHANT_PRICES_RANGE_1].Hide();
                mOptions[RSK_MERCHANT_PRICES_RANGE_2].Hide();
                mOptions[RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT].Hide();
                mOptions[RSK_MERCHANT_PRICES_AFFORDABLE].Unhide();
                break;
        }
    });
    OPT_U8(RSK_MERCHANT_PRICES_FIXED_PRICE, "Merchant Fixed Price", {NumOpts(0, 995, 5)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MerchantFixedPrice"), mOptionDescriptions[RSK_MERCHANT_PRICES_FIXED_PRICE], WIDGET_CVAR_SLIDER_INT, 10, true);
    OPT_U8(RSK_MERCHANT_PRICES_RANGE_1, "Merchant Lower Bound", {NumOpts(0, 995, 5)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MerchantPriceRange1"), mOptionDescriptions[RSK_MERCHANT_PRICES_RANGE_1], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MERCHANT_PRICES_RANGE_2, "Merchant Upper Bound", {NumOpts(0, 995, 5)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MerchantPriceRange2"), mOptionDescriptions[RSK_MERCHANT_PRICES_RANGE_2], WIDGET_CVAR_SLIDER_INT, 100, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT, "Merchant No Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MerchantNoWalletWeight"), mOptionDescriptions[RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT, "Merchant Child Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MerchantChildWalletWeight"), mOptionDescriptions[RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT, "Merchant Adult Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MerchantAdultWalletWeight"), mOptionDescriptions[RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT, "Merchant Giant Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MerchantGiantWalletWeight"), mOptionDescriptions[RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_U8(RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT, "Merchant Tycoon Wallet Weight", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("MerchantTycoonWalletWeight"), mOptionDescriptions[RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT], WIDGET_CVAR_SLIDER_INT, 10, true, nullptr, IMFLAG_NONE);
    OPT_BOOL(RSK_MERCHANT_PRICES_AFFORDABLE, "Merchant Affordable Prices", CVAR_RANDOMIZER_SETTING("MerchantPricesAffordable"), mOptionDescriptions[RSK_MERCHANT_PRICES_AFFORDABLE]);
    OPT_BOOL(RSK_SHUFFLE_BEGGAR, "Shuffle Beggar", CVAR_RANDOMIZER_SETTING("ShuffleBeggar"), mOptionDescriptions[RSK_SHUFFLE_BEGGAR]);
    OPT_BOOL(RSK_SHUFFLE_FROG_SONG_RUPEES, "Shuffle Frog Song Rupees", CVAR_RANDOMIZER_SETTING("ShuffleFrogSongRupees"), mOptionDescriptions[RSK_SHUFFLE_FROG_SONG_RUPEES]);
    OPT_BOOL(RSK_SHUFFLE_ADULT_TRADE, "Shuffle Adult Trade", CVAR_RANDOMIZER_SETTING("ShuffleAdultTrade"), mOptionDescriptions[RSK_SHUFFLE_ADULT_TRADE]);
    OPT_CALLBACK(RSK_SHUFFLE_ADULT_TRADE, {
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleAdultTrade"), RO_GENERIC_OFF)) {
            mOptions[RSK_EARLY_GRANNYS_SHOP].Disable("This has no effect when Shuffle Adult Trade is on.");
        } else {
            mOptions[RSK_EARLY_GRANNYS_SHOP].Enable();
        }
    });
    OPT_U8(RSK_SHUFFLE_CHEST_MINIGAME, "Shuffle Chest Minigame", {"Off", "On (Separate)", "On (Pack)"});
    OPT_BOOL(RSK_SHUFFLE_100_GS_REWARD, "Shuffle 100 GS Reward", CVAR_RANDOMIZER_SETTING("Shuffle100GSReward"), mOptionDescriptions[RSK_SHUFFLE_100_GS_REWARD], IMFLAG_SEPARATOR_BOTTOM, WIDGET_CVAR_CHECKBOX, RO_GENERIC_OFF);
    OPT_CALLBACK(RSK_SHUFFLE_100_GS_REWARD, {
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("Shuffle100GSReward"), RO_GENERIC_OFF)) {
            mOptions[RSK_KAK_100_SKULLS_HINT].Enable();
        } else {
            mOptions[RSK_KAK_100_SKULLS_HINT].Disable("There is no point to hinting 100 skulls if it is not shuffled.");
        }
    });
    OPT_BOOL(RSK_SHUFFLE_BEAN_SOULS, "Shuffle Bean Souls", CVAR_RANDOMIZER_SETTING("ShuffleBeanSouls"), mOptionDescriptions[RSK_SHUFFLE_BEAN_SOULS], IMFLAG_SEPARATOR_BOTTOM, WIDGET_CVAR_CHECKBOX, RO_GENERIC_OFF);
    OPT_U8(RSK_SHUFFLE_BOSS_SOULS, "Shuffle Boss Souls", {"Off", "On", "On + Ganon"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleBossSouls"), mOptionDescriptions[RSK_SHUFFLE_BOSS_SOULS], WIDGET_CVAR_COMBOBOX);
    OPT_BOOL(RSK_SHUFFLE_DEKU_STICK_BAG, "Shuffle Deku Stick Bag", CVAR_RANDOMIZER_SETTING("ShuffleDekuStickBag"), mOptionDescriptions[RSK_SHUFFLE_DEKU_STICK_BAG], IMFLAG_SEPARATOR_BOTTOM, WIDGET_CVAR_CHECKBOX, RO_GENERIC_OFF);
    OPT_CALLBACK(RSK_SHUFFLE_DEKU_STICK_BAG, {
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleDekuStickBag"), 0)) {
            mOptions[RSK_STARTING_STICKS].Disable("Disabled because Shuffle Deku Stick Bag is on.");
        } else {
            mOptions[RSK_STARTING_STICKS].Enable();
        }
    });
    OPT_BOOL(RSK_SHUFFLE_DEKU_NUT_BAG, "Shuffle Deku Nut Bag", CVAR_RANDOMIZER_SETTING("ShuffleDekuNutBag"), mOptionDescriptions[RSK_SHUFFLE_DEKU_NUT_BAG], IMFLAG_SEPARATOR_BOTTOM, WIDGET_CVAR_CHECKBOX, RO_GENERIC_OFF);
    OPT_CALLBACK(RSK_SHUFFLE_DEKU_NUT_BAG, {
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleDekuNutBag"), 0)) {
            mOptions[RSK_STARTING_NUTS].Disable("Disabled because Shuffle Deku Nut Bag is on.");
        } else {
            mOptions[RSK_STARTING_NUTS].Enable();
        }
    });
    OPT_U8(RSK_SHUFFLE_FREESTANDING, "Shuffle Freestanding Items", {"Off", "Dungeons", "Overworld", "All Items"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleFreestanding"), mOptionDescriptions[RSK_SHUFFLE_FREESTANDING], WIDGET_CVAR_COMBOBOX, RO_SHUFFLE_FREESTANDING_OFF);
    OPT_U8(RSK_SHUFFLE_WONDER_ITEMS, "Shuffle Wonder Items", {"Off", "Dungeons", "Overworld", "All Items"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleWonderItems"), mOptionDescriptions[RSK_SHUFFLE_WONDER_ITEMS], WIDGET_CVAR_COMBOBOX, RO_SHUFFLE_WONDER_ITEMS_OFF);
    OPT_U8(RSK_FISHSANITY, "Fishsanity", {"Off", "Shuffle only Hyrule Loach", "Shuffle Fishing Pond", "Shuffle Overworld Fish", "Shuffle Both"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("Fishsanity"), mOptionDescriptions[RSK_FISHSANITY], WIDGET_CVAR_COMBOBOX, RO_FISHSANITY_OFF);
    OPT_CALLBACK(RSK_FISHSANITY, {
        // Hide fishing pond settings if we aren't shuffling the fishing pond
        switch (CVarGetInteger(CVAR_RANDOMIZER_SETTING("Fishsanity"), RO_FISHSANITY_OFF)) {
            case RO_FISHSANITY_POND:
            case RO_FISHSANITY_BOTH:
                mOptions[RSK_FISHSANITY_POND_COUNT].Unhide();
                mOptions[RSK_FISHSANITY_AGE_SPLIT].Unhide();
                break;
            default:
                mOptions[RSK_FISHSANITY_POND_COUNT].Hide();
                mOptions[RSK_FISHSANITY_AGE_SPLIT].Hide();
        }
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("Fishsanity"), RO_FISHSANITY_OFF) == RO_FISHSANITY_HYRULE_LOACH) {
            mOptions[RSK_LOACH_HINT].Enable();
        } else {
            mOptions[RSK_LOACH_HINT].Disable(
                "Loach hint is only avaliable with \"Fishsanity\" set to \"Shuffle only Hyrule Loach\"\nas that's the only "
                "setting where you present the loach to the fishing pond owner.");
        }
    });
    OPT_U8(RSK_FISHSANITY_POND_COUNT, "Pond Fish Count", {NumOpts(0,17,1)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("FishsanityPondCount"), mOptionDescriptions[RSK_FISHSANITY_POND_COUNT], WIDGET_CVAR_SLIDER_INT, 0, true, nullptr, IMFLAG_NONE);
    OPT_BOOL(RSK_FISHSANITY_AGE_SPLIT, "Pond Age Split", CVAR_RANDOMIZER_SETTING("FishsanityAgeSplit"), mOptionDescriptions[RSK_FISHSANITY_AGE_SPLIT]);
    OPT_BOOL(RSK_SHUFFLE_FOUNTAIN_FAIRIES, "Shuffle Fairies in Fountains", CVAR_RANDOMIZER_SETTING("ShuffleFountainFairies"), mOptionDescriptions[RSK_SHUFFLE_FOUNTAIN_FAIRIES]);
    OPT_BOOL(RSK_SHUFFLE_STONE_FAIRIES, "Shuffle Gossip Stone Fairies", CVAR_RANDOMIZER_SETTING("ShuffleStoneFairies"), mOptionDescriptions[RSK_SHUFFLE_STONE_FAIRIES]);
    OPT_BOOL(RSK_SHUFFLE_BEAN_FAIRIES, "Shuffle Bean Fairies", CVAR_RANDOMIZER_SETTING("ShuffleBeanFairies"), mOptionDescriptions[RSK_SHUFFLE_BEAN_FAIRIES]);
    OPT_BOOL(RSK_SHUFFLE_SONG_FAIRIES, "Shuffle Fairy Spots", CVAR_RANDOMIZER_SETTING("ShuffleFairySpots"), mOptionDescriptions[RSK_SHUFFLE_SONG_FAIRIES]);
    OPT_BOOL(RSK_SHUFFLE_BUTTERFLY_FAIRIES, "Shuffle Butterfly Fairies", CVAR_RANDOMIZER_SETTING("ShuffleButterflyFairies"), mOptionDescriptions[RSK_SHUFFLE_BUTTERFLY_FAIRIES]);
    // clang-format on
}

} // namespace Rando
