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

void Settings::CreateInventoryAndLogicOptions() {
    // clang-format off
    OPT_U8(RSK_SHUFFLE_MAPANDCOMPASS, "Maps/Compasses", {"Start With", "Vanilla", "Own Dungeon", "Any Dungeon", "Overworld", "Anywhere"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("StartingMapsCompasses"), mOptionDescriptions[RSK_SHUFFLE_MAPANDCOMPASS], WIDGET_CVAR_COMBOBOX, RO_DUNGEON_ITEM_LOC_OWN_DUNGEON);
    OPT_U8(RSK_KEYSANITY, "Small Key Shuffle", {"Start With", "Vanilla", "Own Dungeon", "Any Dungeon", "Overworld", "Anywhere"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("Keysanity"), mOptionDescriptions[RSK_KEYSANITY], WIDGET_CVAR_COMBOBOX, RO_DUNGEON_ITEM_LOC_OWN_DUNGEON);
    OPT_U8(RSK_GERUDO_KEYS, "Gerudo Fortress Keys", {"Vanilla", "Any Dungeon", "Overworld", "Anywhere"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("GerudoKeys"), mOptionDescriptions[RSK_GERUDO_KEYS], WIDGET_CVAR_COMBOBOX, RO_GERUDO_KEYS_VANILLA);
    OPT_CALLBACK(RSK_GERUDO_KEYS, {
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
    OPT_U8(RSK_BOSS_KEYSANITY, "Boss Key Shuffle", {"Start With", "Vanilla", "Own Dungeon", "Any Dungeon", "Overworld", "Anywhere"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("BossKeysanity"), mOptionDescriptions[RSK_BOSS_KEYSANITY], WIDGET_CVAR_COMBOBOX, RO_DUNGEON_ITEM_LOC_OWN_DUNGEON);
    OPT_U8(RSK_GANONS_BOSS_KEY, "Ganon's Boss Key", {"Vanilla", "Own Dungeon", "Start With", "Any Dungeon", "Overworld", "Anywhere", "LACS-Vanilla", "LACS-Stones", "LACS-Medallions", "LACS-Rewards", "LACS-Dungeons", "LACS-Tokens", "100 GS Reward"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleGanonBossKey"), mOptionDescriptions[RSK_GANONS_BOSS_KEY], WIDGET_CVAR_COMBOBOX, RO_GANON_BOSS_KEY_VANILLA);
    OPT_CALLBACK(RSK_GANONS_BOSS_KEY, {
        // Shuffle 100 GS Reward - Force-Enabled if Ganon's Boss Key is on the 100 GS Reward
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleGanonBossKey"), RO_GANON_BOSS_KEY_VANILLA) ==
            RO_GANON_BOSS_KEY_KAK_TOKENS) {
            mOptions[RSK_SHUFFLE_100_GS_REWARD].Disable(
                "This option is force-enabled because \"Ganon's Boss Key\" is set to \"100 GS Reward\".");
        } else {
            mOptions[RSK_SHUFFLE_100_GS_REWARD].Enable();
        }
        mOptions[RSK_LACS_OPTIONS].Hide();
        mOptions[RSK_LACS_STONE_COUNT].Hide();
        mOptions[RSK_LACS_MEDALLION_COUNT].Hide();
        mOptions[RSK_LACS_REWARD_COUNT].Hide();
        mOptions[RSK_LACS_DUNGEON_COUNT].Hide();
        mOptions[RSK_LACS_TOKEN_COUNT].Hide();
        switch (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleGanonBossKey"), RO_GANON_BOSS_KEY_VANILLA)) {
            case RO_GANON_BOSS_KEY_LACS_STONES:
                mOptions[RSK_LACS_OPTIONS].Unhide();
                mOptions[RSK_LACS_STONE_COUNT].Unhide();
                break;
            case RO_GANON_BOSS_KEY_LACS_MEDALLIONS:
                mOptions[RSK_LACS_OPTIONS].Unhide();
                mOptions[RSK_LACS_MEDALLION_COUNT].Unhide();
                break;
            case RO_GANON_BOSS_KEY_LACS_REWARDS:
                mOptions[RSK_LACS_OPTIONS].Unhide();
                mOptions[RSK_LACS_REWARD_COUNT].Unhide();
                break;
            case RO_GANON_BOSS_KEY_LACS_DUNGEONS:
                mOptions[RSK_LACS_OPTIONS].Unhide();
                mOptions[RSK_LACS_DUNGEON_COUNT].Unhide();
                break;
            case RO_GANON_BOSS_KEY_LACS_TOKENS:
                mOptions[RSK_LACS_TOKEN_COUNT].Unhide();
                break;
        }
    });
    OPT_U8(RSK_LACS_STONE_COUNT, "GCBK Stone Count", {NumOpts(0, 4)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("LacsStoneCount"), "", WIDGET_CVAR_SLIDER_INT, 3, true);
    OPT_U8(RSK_LACS_MEDALLION_COUNT, "GCBK Medallion Count", {NumOpts(0, 7)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("LacsMedallionCount"), "", WIDGET_CVAR_SLIDER_INT, 6, true);
    OPT_U8(RSK_LACS_REWARD_COUNT, "GCBK Reward Count", {NumOpts(0, 10)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("LacsRewardCount"), "", WIDGET_CVAR_SLIDER_INT, 9, true);
    OPT_U8(RSK_LACS_DUNGEON_COUNT, "GCBK Dungeon Count", {NumOpts(0, 9)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("LacsDungeonCount"), "", WIDGET_CVAR_SLIDER_INT, 8, true);
    OPT_U8(RSK_LACS_TOKEN_COUNT, "GCBK Token Count", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("LacsTokenCount"), "", WIDGET_CVAR_SLIDER_INT, 100, true);
    OPT_U8(RSK_LACS_OPTIONS, "GCBK LACS Reward Options", {"Standard Reward", "Greg as Reward", "Greg as Wildcard"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("LacsRewardOptions"), mOptionDescriptions[RSK_LACS_OPTIONS], WIDGET_CVAR_COMBOBOX, RO_LACS_STANDARD_REWARD);
    OPT_CALLBACK(RSK_LACS_OPTIONS, {
        const uint8_t lacsOpts = CVarGetInteger(CVAR_RANDOMIZER_SETTING("LacsRewardOptions"), RO_LACS_STANDARD_REWARD);
        if (lacsOpts == RO_LACS_GREG_REWARD) {
            if (mOptions[RSK_LACS_STONE_COUNT].GetOptionCount() == 4) {
                mOptions[RSK_LACS_STONE_COUNT].ChangeOptions(NumOpts(0, 4));
            }
            if (mOptions[RSK_LACS_MEDALLION_COUNT].GetOptionCount() == 7) {
                mOptions[RSK_LACS_MEDALLION_COUNT].ChangeOptions(NumOpts(0, 7));
            }
            if (mOptions[RSK_LACS_REWARD_COUNT].GetOptionCount() == 10) {
                mOptions[RSK_LACS_REWARD_COUNT].ChangeOptions(NumOpts(0, 10));
            }
            if (mOptions[RSK_LACS_DUNGEON_COUNT].GetOptionCount() == 9) {
                mOptions[RSK_LACS_DUNGEON_COUNT].ChangeOptions(NumOpts(0, 9));
            }
        } else {
            if (mOptions[RSK_LACS_STONE_COUNT].GetOptionCount() == 5) {
                mOptions[RSK_LACS_STONE_COUNT].ChangeOptions(NumOpts(0, 3));
            }
            if (mOptions[RSK_LACS_MEDALLION_COUNT].GetOptionCount() == 8) {
                mOptions[RSK_LACS_MEDALLION_COUNT].ChangeOptions(NumOpts(0, 6));
            }
            if (mOptions[RSK_LACS_REWARD_COUNT].GetOptionCount() == 11) {
                mOptions[RSK_LACS_REWARD_COUNT].ChangeOptions(NumOpts(0, 9));
            }
            if (mOptions[RSK_LACS_DUNGEON_COUNT].GetOptionCount() == 10) {
                mOptions[RSK_LACS_DUNGEON_COUNT].ChangeOptions(NumOpts(0, 8));
            }
        }
    });
    OPT_U8(RSK_KEYRINGS, "Key Rings", {"Off", "Random", "Count", "Selection"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleKeyRings"), mOptionDescriptions[RSK_KEYRINGS], WIDGET_CVAR_COMBOBOX, RO_KEYRINGS_OFF);
    OPT_CALLBACK(RSK_KEYRINGS, {
        switch (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleKeyRings"), RO_KEYRINGS_OFF)) {
            case RO_KEYRINGS_COUNT:
                // Show count slider.
                mOptions[RSK_KEYRINGS_RANDOM_COUNT].Unhide();
                mOptions[RSK_KEYRINGS_GERUDO_FORTRESS].Hide();
                mOptions[RSK_KEYRINGS_FOREST_TEMPLE].Hide();
                mOptions[RSK_KEYRINGS_FIRE_TEMPLE].Hide();
                mOptions[RSK_KEYRINGS_WATER_TEMPLE].Hide();
                mOptions[RSK_KEYRINGS_SPIRIT_TEMPLE].Hide();
                mOptions[RSK_KEYRINGS_SHADOW_TEMPLE].Hide();
                mOptions[RSK_KEYRINGS_BOTTOM_OF_THE_WELL].Hide();
                mOptions[RSK_KEYRINGS_GTG].Hide();
                mOptions[RSK_KEYRINGS_GANONS_CASTLE].Hide();
                break;
            case RO_KEYRINGS_SELECTION:
                // Show checkboxes for each dungeon with keys.
                mOptions[RSK_KEYRINGS_RANDOM_COUNT].Hide();
                mOptions[RSK_KEYRINGS_GERUDO_FORTRESS].Unhide();
                mOptions[RSK_KEYRINGS_FOREST_TEMPLE].Unhide();
                mOptions[RSK_KEYRINGS_FIRE_TEMPLE].Unhide();
                mOptions[RSK_KEYRINGS_WATER_TEMPLE].Unhide();
                mOptions[RSK_KEYRINGS_SPIRIT_TEMPLE].Unhide();
                mOptions[RSK_KEYRINGS_SHADOW_TEMPLE].Unhide();
                mOptions[RSK_KEYRINGS_BOTTOM_OF_THE_WELL].Unhide();
                mOptions[RSK_KEYRINGS_GTG].Unhide();
                mOptions[RSK_KEYRINGS_GANONS_CASTLE].Unhide();
                break;
            default:
                mOptions[RSK_KEYRINGS_RANDOM_COUNT].Hide();
                mOptions[RSK_KEYRINGS_GERUDO_FORTRESS].Hide();
                mOptions[RSK_KEYRINGS_FOREST_TEMPLE].Hide();
                mOptions[RSK_KEYRINGS_FIRE_TEMPLE].Hide();
                mOptions[RSK_KEYRINGS_WATER_TEMPLE].Hide();
                mOptions[RSK_KEYRINGS_SPIRIT_TEMPLE].Hide();
                mOptions[RSK_KEYRINGS_SHADOW_TEMPLE].Hide();
                mOptions[RSK_KEYRINGS_BOTTOM_OF_THE_WELL].Hide();
                mOptions[RSK_KEYRINGS_GTG].Hide();
                mOptions[RSK_KEYRINGS_GANONS_CASTLE].Hide();
                break;
        }
    });
    OPT_U8(RSK_KEYRINGS_RANDOM_COUNT, "Keyring Dungeon Count", {NumOpts(0, 9)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleKeyRingsRandomCount"), "", WIDGET_CVAR_SLIDER_INT, 8);
    OPT_U8(RSK_KEYRINGS_GERUDO_FORTRESS, "Gerudo Fortress Keyring", {"No", "Random", "Yes"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleKeyRingsGerudoFortress"), "", WIDGET_CVAR_COMBOBOX, 0);
    OPT_U8(RSK_KEYRINGS_FOREST_TEMPLE, "Forest Temple Keyring", {"No", "Random", "Yes"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleKeyRingsForestTemple"), "", WIDGET_CVAR_COMBOBOX, 0);
    OPT_U8(RSK_KEYRINGS_FIRE_TEMPLE, "Fire Temple Keyring", {"No", "Random", "Yes"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleKeyRingsFireTemple"), "", WIDGET_CVAR_COMBOBOX, 0);
    OPT_U8(RSK_KEYRINGS_WATER_TEMPLE, "Water Temple Keyring", {"No", "Random", "Yes"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleKeyRingsWaterTemple"), "", WIDGET_CVAR_COMBOBOX, 0);
    OPT_U8(RSK_KEYRINGS_SPIRIT_TEMPLE, "Spirit Temple Keyring", {"No", "Random", "Yes"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleKeyRingsSpiritTemple"), "", WIDGET_CVAR_COMBOBOX, 0);
    OPT_U8(RSK_KEYRINGS_SHADOW_TEMPLE, "Shadow Temple Keyring", {"No", "Random", "Yes"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleKeyRingsShadowTemple"), "", WIDGET_CVAR_COMBOBOX, 0);
    OPT_U8(RSK_KEYRINGS_BOTTOM_OF_THE_WELL, "Bottom of the Well Keyring", {"No", "Random", "Yes"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleKeyRingsBottomOfTheWell"), "", WIDGET_CVAR_COMBOBOX, 0);
    OPT_U8(RSK_KEYRINGS_GTG, "Gerudo Training Ground Keyring", {"No", "Random", "Yes"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleKeyRingsGTG"), "", WIDGET_CVAR_COMBOBOX, 0);
    OPT_U8(RSK_KEYRINGS_GANONS_CASTLE, "Ganon's Castle Keyring", {"No", "Random", "Yes"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ShuffleKeyRingsGanonsCastle"), "", WIDGET_CVAR_COMBOBOX, 0);
    //Dummied out due to redundancy with TimeSavers.SkipChildStealth until such a time that logic needs to consider child stealth e.g. because it's freestanding checks are added to freestanding shuffle.
    //To undo this dummying, readd this setting to an OptionGroup so it appears in the UI, then edit the timesaver check hooks to look at this, and the timesaver setting to lock itself as needed.
    OPT_BOOL(RSK_SKIP_CHILD_STEALTH, "Skip Child Stealth", {"Don't Skip", "Skip"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("SkipChildStealth"), mOptionDescriptions[RSK_SKIP_CHILD_STEALTH], WIDGET_CVAR_CHECKBOX, RO_GENERIC_DONT_SKIP);
    OPT_BOOL(RSK_SKIP_CHILD_ZELDA, "Skip Child Zelda", {"Don't Skip", "Skip"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("SkipChildZelda"), mOptionDescriptions[RSK_SKIP_CHILD_ZELDA], WIDGET_CVAR_CHECKBOX, RO_GENERIC_DONT_SKIP);
    OPT_CALLBACK(RSK_SKIP_CHILD_ZELDA, {
        // Shuffle Weird Egg - Disabled when Skip Child Zelda is active
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("SkipChildZelda"), RO_GENERIC_DONT_SKIP)) {
            mOptions[RSK_SHUFFLE_WEIRD_EGG].Disable("This option is disabled because \"Skip Child Zelda\" is enabled.");
            mOptions[RSK_SKIP_CHILD_STEALTH].Disable("This option is disabled because \"Skip Child Zelda\" is enabled.");
        } else {
            mOptions[RSK_SHUFFLE_WEIRD_EGG].Enable();
            mOptions[RSK_SKIP_CHILD_STEALTH].Enable();
        }
    });
    OPT_BOOL(RSK_EARLY_GRANNYS_SHOP, "Early Granny's Potion Shop", CVAR_RANDOMIZER_SETTING("EarlyGrannysShop"), mOptionDescriptions[RSK_EARLY_GRANNYS_SHOP]);
    OPT_BOOL(RSK_SKIP_EPONA_RACE, "Skip Epona Race", {"Don't Skip", "Skip"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("SkipEponaRace"), mOptionDescriptions[RSK_SKIP_EPONA_RACE], WIDGET_CVAR_CHECKBOX, RO_GENERIC_DONT_SKIP);
    OPT_BOOL(RSK_SKIP_SCARECROWS_SONG, "Skip Scarecrow's Song", CVAR_RANDOMIZER_SETTING("SkipScarecrowsSong"), mOptionDescriptions[RSK_SKIP_SCARECROWS_SONG]);
    OPT_BOOL(RSK_SKIP_PLANTING_BEANS, "Skip Planting Beans", CVAR_RANDOMIZER_SETTING("SkipPlantingBeans"), mOptionDescriptions[RSK_SKIP_PLANTING_BEANS]);
    OPT_U8(RSK_BIG_POE_COUNT, "Big Poe Target Count", {NumOpts(0, 10)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("BigPoeTargetCount"), mOptionDescriptions[RSK_BIG_POE_COUNT], WIDGET_CVAR_SLIDER_INT, 10);
    OPT_CALLBACK(RSK_BIG_POE_COUNT, {
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("BigPoeTargetCount"), 10) == 0) {
            mOptions[RSK_BIG_POES_HINT].Disable("Poe Collector will just give you the item instead with 0 big poes.");
        } else {
            mOptions[RSK_BIG_POES_HINT].Enable();
        }
    });
    OPT_U8(RSK_MASK_QUEST, "Mask Quest", {"Vanilla", "Completed", "Shuffle"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("CompleteMaskQuest"), mOptionDescriptions[RSK_MASK_QUEST], WIDGET_CVAR_COMBOBOX, 0);
    OPT_U8(RSK_GOSSIP_STONE_HINTS, "Gossip Stone Hints", {"No Hints", "Need Nothing", "Mask of Truth", "Stone of Agony"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("GossipStoneHints"), mOptionDescriptions[RSK_GOSSIP_STONE_HINTS], WIDGET_CVAR_COMBOBOX, RO_GOSSIP_STONES_NEED_NOTHING, false, nullptr, IMFLAG_NONE);
    OPT_CALLBACK(RSK_GOSSIP_STONE_HINTS, {
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("GossipStoneHints"), RO_GOSSIP_STONES_NEED_NOTHING) ==
            RO_GOSSIP_STONES_NONE) {
            mOptions[RSK_HINT_CLARITY].Hide();
            mOptions[RSK_HINT_DISTRIBUTION].Hide();
        } else {
            mOptions[RSK_HINT_CLARITY].Unhide();
            mOptions[RSK_HINT_DISTRIBUTION].Unhide();
        }
    });
    OPT_U8(RSK_HINT_CLARITY, "Hint Clarity", {"Obscure", "Ambiguous", "Clear"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("HintClarity"), mOptionDescriptions[RSK_HINT_CLARITY], WIDGET_CVAR_COMBOBOX, RO_HINT_CLARITY_CLEAR, true, nullptr, IMFLAG_INDENT);
    OPT_U8(RSK_HINT_DISTRIBUTION, "Hint Distribution", {"Useless", "Balanced", "Strong", "Very Strong"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("HintDistribution"), mOptionDescriptions[RSK_HINT_DISTRIBUTION], WIDGET_CVAR_COMBOBOX, RO_HINT_DIST_BALANCED, true, nullptr, IMFLAG_UNINDENT);
    OPT_BOOL(RSK_TOT_ALTAR_HINT, "ToT Altar Hint", {"Off", "On"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("AltarHint"), mOptionDescriptions[RSK_TOT_ALTAR_HINT], WIDGET_CVAR_CHECKBOX, RO_GENERIC_ON, false, nullptr, IMFLAG_INDENT);
    OPT_BOOL(RSK_GANONDORF_HINT, "Ganondorf Hint", {"Off", "On"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("GanondorfHint"), mOptionDescriptions[RSK_GANONDORF_HINT], WIDGET_CVAR_CHECKBOX, RO_GENERIC_ON, false, nullptr, IMFLAG_NONE);
    OPT_BOOL(RSK_SHEIK_LA_HINT, "Sheik Light Arrow Hint", {"Off", "On"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("SheikLAHint"), mOptionDescriptions[RSK_SHEIK_LA_HINT], WIDGET_CVAR_CHECKBOX, RO_GENERIC_ON, false, nullptr, IMFLAG_NONE);
    OPT_BOOL(RSK_BOSS_KEY_HINT, "Boss Door Hints", CVAR_RANDOMIZER_SETTING("BossKeyHint"), mOptionDescriptions[RSK_BOSS_KEY_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_DAMPES_DIARY_HINT, "Dampe's Diary Hint", CVAR_RANDOMIZER_SETTING("DampeHint"), mOptionDescriptions[RSK_DAMPES_DIARY_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_GREG_HINT, "Greg the Green Rupee Hint", CVAR_RANDOMIZER_SETTING("GregHint"), mOptionDescriptions[RSK_GREG_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_LOACH_HINT, "Hyrule Loach Hint", CVAR_RANDOMIZER_SETTING("LoachHint"), mOptionDescriptions[RSK_LOACH_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_SARIA_HINT, "Saria's Hint", CVAR_RANDOMIZER_SETTING("SariaHint"), mOptionDescriptions[RSK_SARIA_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_MIDO_HINT, "Mido's Hint", CVAR_RANDOMIZER_SETTING("MidoHint"), mOptionDescriptions[RSK_MIDO_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_FISHING_POLE_HINT, "Fishing Pole Hint", CVAR_RANDOMIZER_SETTING("FishingPoleHint"), mOptionDescriptions[RSK_FISHING_POLE_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_FROGS_HINT, "Frog Ocarina Game Hint", CVAR_RANDOMIZER_SETTING("FrogsHint"), mOptionDescriptions[RSK_FROGS_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_OOT_HINT, "Ocarina of Time Hint", CVAR_RANDOMIZER_SETTING("OoTHint"), mOptionDescriptions[RSK_OOT_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_BIGGORON_HINT, "Biggoron's Hint", CVAR_RANDOMIZER_SETTING("BiggoronHint"), mOptionDescriptions[RSK_BIGGORON_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_BIG_POES_HINT, "Big Poes Hint", CVAR_RANDOMIZER_SETTING("BigPoesHint"), mOptionDescriptions[RSK_BIG_POES_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_CHICKENS_HINT, "Chickens Hint", CVAR_RANDOMIZER_SETTING("ChickensHint"), mOptionDescriptions[RSK_CHICKENS_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_MALON_HINT, "Malon Hint", CVAR_RANDOMIZER_SETTING("MalonHint"), mOptionDescriptions[RSK_MALON_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_HBA_HINT, "Horseback Archery Hint", CVAR_RANDOMIZER_SETTING("HBAHint"), mOptionDescriptions[RSK_HBA_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_WARP_SONG_HINTS, "Warp Song Hints", CVAR_RANDOMIZER_SETTING("WarpSongText"), mOptionDescriptions[RSK_WARP_SONG_HINTS], IMFLAG_NONE, WIDGET_CVAR_CHECKBOX, RO_GENERIC_ON);
    OPT_BOOL(RSK_SCRUB_TEXT_HINT, "Scrub Hint Text", CVAR_RANDOMIZER_SETTING("ScrubText"), mOptionDescriptions[RSK_SCRUB_TEXT_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_MERCHANT_TEXT_HINT, "Merchant Hint Text", CVAR_RANDOMIZER_SETTING("MerchantText"), mOptionDescriptions[RSK_MERCHANT_TEXT_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_KAK_10_SKULLS_HINT, "10 GS Hint", CVAR_RANDOMIZER_SETTING("10GSHint"), mOptionDescriptions[RSK_KAK_10_SKULLS_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_KAK_20_SKULLS_HINT, "20 GS Hint", CVAR_RANDOMIZER_SETTING("20GSHint"), mOptionDescriptions[RSK_KAK_20_SKULLS_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_KAK_30_SKULLS_HINT, "30 GS Hint", CVAR_RANDOMIZER_SETTING("30GSHint"), mOptionDescriptions[RSK_KAK_30_SKULLS_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_KAK_40_SKULLS_HINT, "40 GS Hint", CVAR_RANDOMIZER_SETTING("40GSHint"), mOptionDescriptions[RSK_KAK_40_SKULLS_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_KAK_50_SKULLS_HINT, "50 GS Hint", CVAR_RANDOMIZER_SETTING("50GSHint"), mOptionDescriptions[RSK_KAK_50_SKULLS_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_KAK_100_SKULLS_HINT, "100 GS Hint", CVAR_RANDOMIZER_SETTING("100GSHint"), mOptionDescriptions[RSK_KAK_100_SKULLS_HINT], IMFLAG_NONE);
    OPT_BOOL(RSK_MASK_SHOP_HINT, "Mask Shop Hint", CVAR_RANDOMIZER_SETTING("MaskShopHint"), mOptionDescriptions[RSK_MASK_SHOP_HINT]);
    // TODO: Compasses show rewards/woth, maps show dungeon mode
    OPT_BOOL(RSK_BLUE_FIRE_ARROWS, "Blue Fire Arrows", CVAR_RANDOMIZER_SETTING("BlueFireArrows"), mOptionDescriptions[RSK_BLUE_FIRE_ARROWS]);
    OPT_BOOL(RSK_SUNLIGHT_ARROWS, "Sunlight Arrows", CVAR_RANDOMIZER_SETTING("SunlightArrows"), mOptionDescriptions[RSK_SUNLIGHT_ARROWS]);
    OPT_BOOL(RSK_ROCS_FEATHER, "Roc's Feather", CVAR_RANDOMIZER_SETTING("RocsFeather"), mOptionDescriptions[RSK_ROCS_FEATHER]);
    OPT_U8(RSK_INFINITE_UPGRADES, "Infinite Upgrades", {"Off", "Progressive", "Condensed Progressive"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("InfiniteUpgrades"), mOptionDescriptions[RSK_INFINITE_UPGRADES]);
    OPT_BOOL(RSK_SKELETON_KEY, "Skeleton Key", CVAR_RANDOMIZER_SETTING("SkeletonKey"), mOptionDescriptions[RSK_SKELETON_KEY]);
    OPT_BOOL(RSK_SLINGBOW_BREAK_BEEHIVES, "Slingshot/Bow Can Break Beehives", CVAR_RANDOMIZER_SETTING("SlingBowBeehives"), mOptionDescriptions[RSK_SLINGBOW_BREAK_BEEHIVES]);
    OPT_U8(RSK_ITEM_POOL, "Item Pool", {"Plentiful", "Balanced", "Scarce", "Minimal"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("ItemPool"), mOptionDescriptions[RSK_ITEM_POOL], WIDGET_CVAR_COMBOBOX, RO_ITEM_POOL_BALANCED);
    OPT_BOOL(RSK_BASE_ICE_TRAPS, "Base Ice Traps", CVAR_RANDOMIZER_SETTING("BaseIceTraps"), mOptionDescriptions[RSK_BASE_ICE_TRAPS], IMFLAG_NONE, WIDGET_CVAR_COMBOBOX, RO_GENERIC_ON);
    OPT_U8(RSK_ADDITIONAL_ICE_TRAPS, "Additional Ice Traps", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("AdditionalIceTraps"), mOptionDescriptions[RSK_ADDITIONAL_ICE_TRAPS], WIDGET_CVAR_SLIDER_INT, 0);
    OPT_U8(RSK_ICE_TRAP_PERCENT, "Ice Trap Percent", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("IceTrapPercent"), mOptionDescriptions[RSK_ICE_TRAP_PERCENT], WIDGET_CVAR_SLIDER_INT, 0);
    // TODO: Remove Double Defense, Progressive Goron Sword
    OPT_U8(RSK_STARTING_OCARINA, "Start with Ocarina", {"Off", "Fairy Ocarina", "Ocarina of Time"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("StartingOcarina"), "", WIDGET_CVAR_COMBOBOX, RO_STARTING_OCARINA_OFF);
    OPT_BOOL(RSK_STARTING_DEKU_SHIELD, "Start with Deku Shield", CVAR_RANDOMIZER_SETTING("StartingDekuShield"));
    OPT_BOOL(RSK_STARTING_KOKIRI_SWORD, "Start with Kokiri Sword", CVAR_RANDOMIZER_SETTING("StartingKokiriSword"));
    OPT_BOOL(RSK_STARTING_MASTER_SWORD, "Start with Master Sword", CVAR_RANDOMIZER_SETTING("StartingMasterSword"));
    OPT_BOOL(RSK_STARTING_STICKS, "Start with Stick Ammo", {"No", "Yes"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("StartingSticks"), "", WIDGET_CVAR_CHECKBOX, RO_GENERIC_OFF);
    OPT_BOOL(RSK_STARTING_NUTS, "Start with Nut Ammo", {"No", "Yes"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("StartingNuts"), "", WIDGET_CVAR_CHECKBOX, RO_GENERIC_OFF);
    OPT_BOOL(RSK_STARTING_BEANS, "Start with Magic Beans", {"No", "Yes"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("StartingBeans"), "", WIDGET_CVAR_CHECKBOX, RO_GENERIC_OFF);
    OPT_BOOL(RSK_FULL_WALLETS, "Full Wallets", {"No", "Yes"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("FullWallets"), mOptionDescriptions[RSK_FULL_WALLETS], WIDGET_CVAR_CHECKBOX, RO_GENERIC_OFF);
    OPT_BOOL(RSK_STARTING_ZELDAS_LULLABY, "Start with Zelda's Lullaby", CVAR_RANDOMIZER_SETTING("StartingZeldasLullaby"), "", IMFLAG_NONE);
    OPT_BOOL(RSK_STARTING_EPONAS_SONG, "Start with Epona's Song", CVAR_RANDOMIZER_SETTING("StartingEponasSong"), "", IMFLAG_NONE);
    OPT_BOOL(RSK_STARTING_SARIAS_SONG, "Start with Saria's Song", CVAR_RANDOMIZER_SETTING("StartingSariasSong"), "", IMFLAG_NONE);
    OPT_BOOL(RSK_STARTING_SUNS_SONG, "Start with Sun's Song", CVAR_RANDOMIZER_SETTING("StartingSunsSong"), "", IMFLAG_NONE);
    OPT_BOOL(RSK_STARTING_SONG_OF_TIME, "Start with Song of Time", CVAR_RANDOMIZER_SETTING("StartingSongOfTime"), "", IMFLAG_NONE);
    OPT_BOOL(RSK_STARTING_SONG_OF_STORMS, "Start with Song of Storms", CVAR_RANDOMIZER_SETTING("StartingSongOfStorms"), "", IMFLAG_NONE);
    OPT_BOOL(RSK_STARTING_MINUET_OF_FOREST, "Start with Minuet of Forest", CVAR_RANDOMIZER_SETTING("StartingMinuetOfForest"), "", IMFLAG_NONE);
    OPT_BOOL(RSK_STARTING_BOLERO_OF_FIRE, "Start with Bolero of Fire", CVAR_RANDOMIZER_SETTING("StartingBoleroOfFire"), "", IMFLAG_NONE);
    OPT_BOOL(RSK_STARTING_SERENADE_OF_WATER, "Start with Serenade of Water", CVAR_RANDOMIZER_SETTING("StartingSerenadeOfWater"), "", IMFLAG_NONE);
    OPT_BOOL(RSK_STARTING_REQUIEM_OF_SPIRIT, "Start with Requiem of Spirit", CVAR_RANDOMIZER_SETTING("StartingRequiemOfSpirit"), "", IMFLAG_NONE);
    OPT_BOOL(RSK_STARTING_NOCTURNE_OF_SHADOW, "Start with Nocturne of Shadow", CVAR_RANDOMIZER_SETTING("StartingNocturneOfShadow"), "", IMFLAG_NONE);
    OPT_BOOL(RSK_STARTING_PRELUDE_OF_LIGHT, "Start with Prelude of Light", CVAR_RANDOMIZER_SETTING("StartingPreludeOfLight"));
    OPT_U8(RSK_STARTING_SKULLTULA_TOKEN, "Gold Skulltula Tokens", {NumOpts(0, 100)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("StartingSkulltulaToken"), "", WIDGET_CVAR_SLIDER_INT);
    OPT_U8(RSK_STARTING_HEARTS, "Starting Hearts", {NumOpts(1, 20)}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("StartingHearts"), "", WIDGET_CVAR_SLIDER_INT, 2);
    // TODO: Remainder of Starting Items
    OPT_U8(RSK_LOGIC_RULES, "Logic", {"Glitchless", "No Logic"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("LogicRules"), mOptionDescriptions[RSK_LOGIC_RULES], WIDGET_CVAR_COMBOBOX, RO_LOGIC_GLITCHLESS, false, nullptr, IMFLAG_LABEL_INLINE);
    OPT_CALLBACK(RSK_LOGIC_RULES, {
        HandleStartingAgeUI();
        if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("LogicRules"), RO_LOGIC_GLITCHLESS) != RO_LOGIC_NO_LOGIC &&
            CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShopsanityCount"), 0) > 7) {
            CVarSetInteger(CVAR_RANDOMIZER_SETTING("ShopsanityCount"), 7);
        }
    });
    OPT_BOOL(RSK_ALL_LOCATIONS_REACHABLE, "All Locations Reachable", {"Off", "On"}, OptionCategory::Setting, CVAR_RANDOMIZER_SETTING("AllLocationsReachable"), mOptionDescriptions[RSK_ALL_LOCATIONS_REACHABLE], WIDGET_CVAR_CHECKBOX, RO_GENERIC_ON, false, nullptr, IMFLAG_SAME_LINE);
    OPT_BOOL(RSK_SKULLS_SUNS_SONG, "Night Skulltula's Expect Sun's Song", CVAR_RANDOMIZER_SETTING("GsExpectSunsSong"), mOptionDescriptions[RSK_SKULLS_SUNS_SONG]);
    OPT_U8(RSK_DAMAGE_MULTIPLIER, "Damage Multiplier", {"x1/2", "x1", "x2", "x4", "x8", "x16", "OHKO"}, OptionCategory::Setting, "", "", WIDGET_CVAR_SLIDER_INT, RO_DAMAGE_MULTIPLIER_DEFAULT);
    // Don't show any MQ options if both quests aren't available
    if (!(OTRGlobals::Instance->HasMasterQuest() && OTRGlobals::Instance->HasOriginal())) {
        mOptions[RSK_MQ_DUNGEON_RANDOM].Disable("This option has been disabled because only one type of O2R has been loaded");
        mOptions[RSK_MQ_DUNGEON_COUNT].Disable("This option has been disabled because only one type of O2R has been loaded");
        mOptions[RSK_MQ_DUNGEON_SET].Disable("This option has been disabled because only one type of O2R has been loaded");
        mOptions[RSK_MQ_DEKU_TREE].Disable("This option has been disabled because only one type of O2R has been loaded");
        mOptions[RSK_MQ_DODONGOS_CAVERN].Disable("This option has been disabled because only one type of O2R has been loaded");
        mOptions[RSK_MQ_JABU_JABU].Disable("This option has been disabled because only one type of O2R has been loaded");
        mOptions[RSK_MQ_FOREST_TEMPLE].Disable("This option has been disabled because only one type of O2R has been loaded");
        mOptions[RSK_MQ_FIRE_TEMPLE].Disable("This option has been disabled because only one type of O2R has been loaded");
        mOptions[RSK_MQ_WATER_TEMPLE].Disable("This option has been disabled because only one type of O2R has been loaded");
        mOptions[RSK_MQ_SPIRIT_TEMPLE].Disable("This option has been disabled because only one type of O2R has been loaded");
        mOptions[RSK_MQ_SHADOW_TEMPLE].Disable("This option has been disabled because only one type of O2R has been loaded");
        mOptions[RSK_MQ_BOTTOM_OF_THE_WELL].Disable("This option has been disabled because only one type of O2R has been loaded");
        mOptions[RSK_MQ_ICE_CAVERN].Disable("This option has been disabled because only one type of O2R has been loaded");
        mOptions[RSK_MQ_GTG].Disable("This option has been disabled because only one type of O2R has been loaded");
        mOptions[RSK_MQ_GANONS_CASTLE].Disable("This option has been disabled because only one type of O2R has been loaded");
    } else {
        // If any MQ Options are available, show the MQ Dungeon Randomization Combobox
        mOptions[RSK_MQ_DUNGEON_RANDOM].Enable();
        mOptions[RSK_MQ_DUNGEON_COUNT].Enable();
        mOptions[RSK_MQ_DUNGEON_SET].Enable();
        mOptions[RSK_MQ_DEKU_TREE].Enable();
        mOptions[RSK_MQ_DODONGOS_CAVERN].Enable();
        mOptions[RSK_MQ_JABU_JABU].Enable();
        mOptions[RSK_MQ_FOREST_TEMPLE].Enable();
        mOptions[RSK_MQ_FIRE_TEMPLE].Enable();
        mOptions[RSK_MQ_WATER_TEMPLE].Enable();
        mOptions[RSK_MQ_SPIRIT_TEMPLE].Enable();
        mOptions[RSK_MQ_SHADOW_TEMPLE].Enable();
        mOptions[RSK_MQ_BOTTOM_OF_THE_WELL].Enable();
        mOptions[RSK_MQ_ICE_CAVERN].Enable();
        mOptions[RSK_MQ_GTG].Enable();
        mOptions[RSK_MQ_GANONS_CASTLE].Enable();
    }
    // clang-format on
}

} // namespace Rando
