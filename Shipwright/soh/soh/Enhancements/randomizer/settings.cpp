#include "soh/OTRGlobals.h"
#include "settings.h"
#include "settings_option_lists.h"
#include <cstdio>
#include "trial.h"
#include "dungeon.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/randomizer/3drando/random.hpp"

#include <spdlog/spdlog.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/libultraship.h>

namespace Rando {
std::shared_ptr<Settings> Settings::mInstance;

void Settings::HandleShopsanityPriceUI() {
    bool isTycoon = CVarGetInteger(CVAR_RANDOMIZER_SETTING("IncludeTycoonWallet"), RO_GENERIC_OFF);
    mOptions[RSK_SHOPSANITY].RemoveFlag(IMFLAG_SEPARATOR_BOTTOM);
    mOptions[RSK_SHOPSANITY_PRICES].Unhide();
    switch (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShopsanityPrices"), RO_PRICE_VANILLA)) {
        case RO_PRICE_FIXED:
            mOptions[RSK_SHOPSANITY_PRICES_FIXED_PRICE].Unhide();
            mOptions[RSK_SHOPSANITY_PRICES_RANGE_1].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_RANGE_2].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_NO_WALLET_WEIGHT].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_CHILD_WALLET_WEIGHT].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_ADULT_WALLET_WEIGHT].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_GIANT_WALLET_WEIGHT].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_TYCOON_WALLET_WEIGHT].Hide();
            if (isTycoon ? mOptions[RSK_SHOPSANITY_PRICES_FIXED_PRICE].GetOptionCount() == 501
                         : mOptions[RSK_SHOPSANITY_PRICES_FIXED_PRICE].GetOptionCount() == 1000) {
                mOptions[RSK_SHOPSANITY_PRICES_FIXED_PRICE].ChangeOptions(isTycoon ? NumOpts(0, 999) : NumOpts(0, 500));
            }
            mOptions[RSK_SHOPSANITY_PRICES_AFFORDABLE].Hide();
            break;
        case RO_PRICE_RANGE:
            mOptions[RSK_SHOPSANITY_PRICES_FIXED_PRICE].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_RANGE_1].Unhide();
            mOptions[RSK_SHOPSANITY_PRICES_RANGE_2].Unhide();
            mOptions[RSK_SHOPSANITY_PRICES_NO_WALLET_WEIGHT].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_CHILD_WALLET_WEIGHT].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_ADULT_WALLET_WEIGHT].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_GIANT_WALLET_WEIGHT].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_TYCOON_WALLET_WEIGHT].Hide();
            if (isTycoon ? mOptions[RSK_SHOPSANITY_PRICES_RANGE_1].GetOptionCount() == 101
                         : mOptions[RSK_SHOPSANITY_PRICES_RANGE_1].GetOptionCount() == 200) {
                mOptions[RSK_SHOPSANITY_PRICES_RANGE_1].ChangeOptions(isTycoon ? NumOpts(0, 995, 5)
                                                                               : NumOpts(0, 500, 5));
                mOptions[RSK_SHOPSANITY_PRICES_RANGE_2].ChangeOptions(isTycoon ? NumOpts(0, 995, 5)
                                                                               : NumOpts(0, 500, 5));
            }
            mOptions[RSK_SHOPSANITY_PRICES_AFFORDABLE].Unhide();
            break;
        case RO_PRICE_SET_BY_WALLET:
            mOptions[RSK_SHOPSANITY_PRICES_FIXED_PRICE].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_RANGE_1].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_RANGE_2].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_NO_WALLET_WEIGHT].Unhide();
            mOptions[RSK_SHOPSANITY_PRICES_CHILD_WALLET_WEIGHT].Unhide();
            mOptions[RSK_SHOPSANITY_PRICES_ADULT_WALLET_WEIGHT].Unhide();
            mOptions[RSK_SHOPSANITY_PRICES_GIANT_WALLET_WEIGHT].Unhide();
            if (isTycoon) {
                mOptions[RSK_SHOPSANITY_PRICES_TYCOON_WALLET_WEIGHT].Unhide();
            } else {
                mOptions[RSK_SHOPSANITY_PRICES_TYCOON_WALLET_WEIGHT].Hide();
            }
            mOptions[RSK_SHOPSANITY_PRICES_AFFORDABLE].Unhide();
            break;
        default:
            mOptions[RSK_SHOPSANITY_PRICES_FIXED_PRICE].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_RANGE_1].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_RANGE_2].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_NO_WALLET_WEIGHT].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_CHILD_WALLET_WEIGHT].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_ADULT_WALLET_WEIGHT].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_GIANT_WALLET_WEIGHT].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_TYCOON_WALLET_WEIGHT].Hide();
            mOptions[RSK_SHOPSANITY_PRICES_AFFORDABLE].Unhide();
            break;
    }
}

Settings::Settings() : mExcludeLocationsOptionsAreas(RCAREA_INVALID) {
}

void Settings::HandleMixedEntrancePoolsUI() {
    bool dungeonShuffle =
        CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleDungeonsEntrances"), RO_DUNGEON_ENTRANCE_SHUFFLE_OFF);
    bool bossShuffle =
        CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleBossEntrances"), RO_BOSS_ROOM_ENTRANCE_SHUFFLE_OFF);
    bool overworldShuffle = CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleOverworldEntrances"), RO_GENERIC_OFF);
    bool interiorShuffle = CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleInteriorsEntrances"), RO_GENERIC_OFF);
    bool grottoShuffle = CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleGrottosEntrances"), RO_GENERIC_OFF);
    bool thievesHideoutShuffle =
        CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleThievesHideoutEntrances"), RO_GENERIC_OFF);

    // Hide Mixed Entrances option if 1 or no applicable entrance shuffles are visible
    if (dungeonShuffle + bossShuffle + overworldShuffle + interiorShuffle + grottoShuffle + thievesHideoutShuffle <=
        1) {
        mOptions[RSK_MIXED_ENTRANCE_POOLS].Hide();
    } else {
        mOptions[RSK_MIXED_ENTRANCE_POOLS].Unhide();
    }
}

void Settings::HandleStartingAgeUI() {
    // Starting Age - Disabled under very specific conditions unless it's No Logic
    if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("LogicRules"), RO_LOGIC_GLITCHLESS) != RO_LOGIC_NO_LOGIC &&
        // If Closed DoT requires OoT then we can only start as child
        ((CVarGetInteger(CVAR_RANDOMIZER_SETTING("DoorOfTime"), RO_DOOROFTIME_CLOSED) == RO_DOOROFTIME_CLOSED &&
          CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleOcarinas"), RO_GENERIC_OFF) == RO_GENERIC_OFF) ||
         // If Forest is Closed, we cannot start as Adult unless there's a sphere 0 entrance shuffle in Kokiri forest,
         // or there's random spawns, as the player may saveload as child and get stuck.
         // Grottos only lead somewhere if decoupled
         (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ClosedForest"), RO_CLOSED_FOREST_ON) == RO_CLOSED_FOREST_ON &&
          CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleOverworldSpawns"), RO_GENERIC_OFF) == RO_GENERIC_OFF &&
          (CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleGrottosEntrances"), RO_GENERIC_OFF) == RO_GENERIC_OFF ||
           CVarGetInteger(CVAR_RANDOMIZER_SETTING("DecoupleEntrances"), RO_GENERIC_OFF) == RO_GENERIC_OFF) &&
          CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleInteriorsEntrances"), RO_GENERIC_OFF) == RO_GENERIC_OFF &&
          CVarGetInteger(CVAR_RANDOMIZER_SETTING("ShuffleOverworldEntrances"), RO_GENERIC_OFF) == RO_GENERIC_OFF))) {
        mOptions[RSK_STARTING_AGE].Disable("This option is disabled due to other options making the game unbeatable.");
    } else {
        mOptions[RSK_STARTING_AGE].Enable();
    }
}

void Settings::CreateOptions() {
    CreateOptionDescriptions();

    CreateWorldAndShuffleOptions();
    CreateInventoryAndLogicOptions();

    StaticData::optionNameToEnum = PopulateOptionNameToEnum();
    mExcludeLocationsOptionsAreas.reserve(RCAREA_INVALID);

    CreateTrickOptions();
    CreateOptionGroups();
}

std::unordered_map<std::string, RandomizerSettingKey> Settings::PopulateOptionNameToEnum() {
    std::unordered_map<std::string, RandomizerSettingKey> output = {};
    for (size_t count = 0; count < RSK_MAX; count++) {
        output[mOptions[count].GetName()] = static_cast<RandomizerSettingKey>(count);
    }
    return output;
}

Option& Settings::GetOption(const RandomizerSettingKey key) {
    return mOptions[key];
}

TrickSetting& Settings::GetTrickSetting(const RandomizerTrick key) {
    return mTrickSettings[key];
}

int Settings::GetRandomizerTrickByName(const std::string& name) {
    const auto& it = mTrickNameToEnum.find(name);
    if (it == mTrickNameToEnum.end()) {
        return -1;
    }
    return it->second;
}

void Context::ResetTrickOptions() {
    for (int count = 0; count < RT_MAX; count++) {
        mTrickOptions[count].Set(0); // RANDOTODO this can probably be done better
    };
}

const std::array<Option, RSK_MAX>& Settings::GetAllOptions() const {
    return mOptions;
}

std::vector<Option*>& Settings::GetExcludeOptionsForArea(const RandomizerCheckArea area) {
    return mExcludeLocationsOptionsAreas[area];
}

const std::vector<std::vector<Option*>>& Settings::GetExcludeLocationsOptions() const {
    return mExcludeLocationsOptionsAreas;
}

const std::array<OptionGroup, RSG_MAX>& Settings::GetOptionGroups() {
    return mOptionGroups;
}

const OptionGroup& Settings::GetOptionGroup(const RandomizerSettingGroupKey key) {
    return mOptionGroups[key];
}

void Settings::UpdateAllOptions() {
    for (auto& option : mOptions) {
        option.RunCallback();
    }
}

void Context::FinalizeSettings(const std::set<RandomizerCheck>& excludedLocations,
                               const std::set<RandomizerTrick>& enabledTricks) {
    // if we skip child zelda, we start with zelda's letter, and malon starts
    // at the ranch, so we should *not* shuffle the weird egg
    if (mOptions[RSK_SKIP_CHILD_ZELDA]) {
        mOptions[RSK_SHUFFLE_WEIRD_EGG].Set(RO_GENERIC_OFF);
    }

    // With certain access settings, the seed is only beatable if Starting Age is set to Child.
    if (mOptions[RSK_LOGIC_RULES].IsNot(RO_LOGIC_NO_LOGIC) &&
        ((mOptions[RSK_DOOR_OF_TIME].Is(RO_DOOROFTIME_CLOSED) && !mOptions[RSK_SHUFFLE_OCARINA]) ||
         (mOptions[RSK_FOREST].Is(RO_CLOSED_FOREST_ON) && mOptions[RSK_SHUFFLE_OVERWORLD_SPAWNS].Is(RO_GENERIC_OFF) &&
          mOptions[RSK_SHUFFLE_OVERWORLD_ENTRANCES].Is(RO_GENERIC_OFF) &&
          mOptions[RSK_SHUFFLE_INTERIOR_ENTRANCES].Is(RO_GENERIC_OFF) &&
          (mOptions[RSK_SHUFFLE_GROTTO_ENTRANCES].Is(RO_GENERIC_OFF) &&
           mOptions[RSK_DECOUPLED_ENTRANCES].Is(RO_GENERIC_OFF))))) {
        mOptions[RSK_STARTING_AGE].Set(RO_AGE_CHILD);
    }

    // Force 100 GS Shuffle if that's where Ganon's Boss Key is
    if (mOptions[RSK_GANONS_BOSS_KEY].Is(RO_GANON_BOSS_KEY_KAK_TOKENS)) {
        mOptions[RSK_SHUFFLE_100_GS_REWARD].Set(1);
    }

    // If we only have MQ, set all dungeons to MQ
    if (OTRGlobals::Instance->HasMasterQuest() && !OTRGlobals::Instance->HasOriginal()) {
        mOptions[RSK_MQ_DUNGEON_RANDOM].Set(RO_MQ_DUNGEONS_SET_NUMBER);
        mOptions[RSK_MQ_DUNGEON_COUNT].Set(MAX_MQ_DUNGEON_COUNT);
        mOptions[RSK_MQ_DUNGEON_SET].Set(RO_GENERIC_OFF);
    }

    // If we don't have MQ, set all dungeons to Vanilla
    if (OTRGlobals::Instance->HasOriginal() && !OTRGlobals::Instance->HasMasterQuest()) {
        mOptions[RSK_MQ_DUNGEON_RANDOM].Set(RO_MQ_DUNGEONS_NONE);
    }

    if (mOptions[RSK_MQ_DUNGEON_RANDOM].Is(RO_MQ_DUNGEONS_NONE)) {
        mOptions[RSK_MQ_DUNGEON_COUNT].Set(0);
        mOptions[RSK_MQ_DUNGEON_SET].Set(RO_GENERIC_OFF);
    }

    // If any of the individual shuffle settings are on, turn on the main Shuffle Entrances option
    if (mOptions[RSK_SHUFFLE_DUNGEON_ENTRANCES].IsNot(RO_DUNGEON_ENTRANCE_SHUFFLE_OFF) ||
        mOptions[RSK_SHUFFLE_BOSS_ENTRANCES].IsNot(RO_BOSS_ROOM_ENTRANCE_SHUFFLE_OFF) ||
        mOptions[RSK_SHUFFLE_OVERWORLD_ENTRANCES] ||
        mOptions[RSK_SHUFFLE_INTERIOR_ENTRANCES].IsNot(RO_INTERIOR_ENTRANCE_SHUFFLE_OFF) ||
        mOptions[RSK_SHUFFLE_THIEVES_HIDEOUT_ENTRANCES] || mOptions[RSK_SHUFFLE_GROTTO_ENTRANCES] ||
        mOptions[RSK_SHUFFLE_OWL_DROPS] || mOptions[RSK_SHUFFLE_WARP_SONGS] || mOptions[RSK_SHUFFLE_OVERWORLD_SPAWNS]) {
        mOptions[RSK_SHUFFLE_ENTRANCES].Set(RO_GENERIC_ON);
    } else {
        mOptions[RSK_SHUFFLE_ENTRANCES].Set(RO_GENERIC_OFF);
    }

    if (mOptions[RSK_SHUFFLE_BOSS_ENTRANCES].Is(RO_BOSS_ROOM_ENTRANCE_SHUFFLE_OFF)) {
        mOptions[RSK_SHUFFLE_GANONS_TOWER_ENTRANCE].Set(RO_GENERIC_OFF);
    }

    if (mOptions[RSK_SHUFFLE_DUNGEON_REWARDS].Is(RO_DUNGEON_REWARDS_END_OF_DUNGEON)) {
        mOptions[RSK_LINKS_POCKET].Set(RO_LINKS_POCKET_DUNGEON_REWARD);
    } else if (mOptions[RSK_SHUFFLE_DUNGEON_REWARDS].Is(RO_DUNGEON_REWARDS_OWN_DUNGEON) ||
               mOptions[RSK_SHUFFLE_DUNGEON_REWARDS].Is(RO_DUNGEON_REWARDS_VANILLA)) {
        mOptions[RSK_LINKS_POCKET_REWARD].Set(RO_LINKS_POCKET_LIGHT_MEDALLION);
    }

    if (mOptions[RSK_LINKS_POCKET].IsNot(RO_LINKS_POCKET_DUNGEON_REWARD)) {
        mOptions[RSK_LINKS_POCKET_REWARD].Set(RO_LINKS_POCKET_ANY_REWARD);
    }

    for (const auto locationKey : this->everyPossibleLocation) {
        if (const auto location = this->GetItemLocation(locationKey);
            excludedLocations.contains(location->GetRandomizerCheck())) {
            location->SetExcludedOption(1);
        } else {
            location->SetExcludedOption(0);
        }
    }
    // Tricks
    ResetTrickOptions();
    for (const auto randomizerTrick : enabledTricks) {
        mTrickOptions[randomizerTrick].Set(1);
    }
    if (!mOptions[RSK_SHUFFLE_KOKIRI_SWORD]) {
        if (mOptions[RSK_STARTING_KOKIRI_SWORD]) {
            this->GetItemLocation(RC_KF_KOKIRI_SWORD_CHEST)->SetExcludedOption(1);
        }
    }
    if (!mOptions[RSK_SHUFFLE_MASTER_SWORD]) {
        if (mOptions[RSK_STARTING_MASTER_SWORD]) {
            this->GetItemLocation(RC_TOT_MASTER_SWORD)->SetExcludedOption(1);
        }
    }
    if (!mOptions[RSK_SHUFFLE_OCARINA]) {
        if (mOptions[RSK_STARTING_OCARINA].IsNot(RO_STARTING_OCARINA_OFF)) {
            this->GetItemLocation(RC_LW_GIFT_FROM_SARIA)->SetExcludedOption(1);
            if (mOptions[RSK_STARTING_OCARINA].Is(RO_STARTING_OCARINA_TIME)) {
                this->GetItemLocation(RC_HF_OCARINA_OF_TIME_ITEM)->SetExcludedOption(1);
            }
        }
    }

    if (mOptions[RSK_SHUFFLE_DEKU_STICK_BAG]) {
        mOptions[RSK_STARTING_STICKS].Set(false);
    }
    if (mOptions[RSK_SHUFFLE_DEKU_NUT_BAG]) {
        mOptions[RSK_STARTING_NUTS].Set(false);
    }

    // RANDOTODO implement chest shuffle with keysanity
    // ShuffleChestMinigame.Set(cvarSettings[RSK_SHUFFLE_CHEST_MINIGAME]);
    mOptions[RSK_SHUFFLE_CHEST_MINIGAME].Set(RO_CHEST_GAME_OFF);

    // TODO: RandomizeAllSettings(true) when implementing the ability to randomize the options themselves.
    std::array<DungeonInfo*, 12> dungeons = this->GetDungeons()->GetDungeonList();

    // reset the MQ vars
    for (auto dungeon : dungeons) {
        dungeon->ClearMQ();
        dungeon->SetDungeonKnown(true);
    }
    // if it's selection mode, process the selection directly
    if (mOptions[RSK_MQ_DUNGEON_RANDOM].Is(RO_MQ_DUNGEONS_SELECTION)) {
        mOptions[RSK_MQ_DUNGEON_SET].Set(RO_GENERIC_ON);
        // How many dungeons are set to MQ in selection
        uint8_t mqSet = 0;
        for (auto dungeon : dungeons) {
            switch (mOptions[dungeon->GetMQSetting()].Get()) {
                case RO_MQ_SET_MQ:
                    dungeon->SetMQ();
                    mqSet += 1;
                    break;
                case RO_MQ_SET_RANDOM:
                    // 50% per dungeon, rolled separatly so people can either have a linear distribtuion
                    // or a bell curve for the number of MQ dungeons per seed.
                    if (Random(0, 2)) {
                        dungeon->SetMQ();
                        mqSet += 1;
                    }
                    dungeon->SetDungeonKnown(false);
                    break;
                default:
                    break;
            }
        }
        // override the dungeons set with the ones set by selection, so it's accurate for anything that wants to know MQ
        // dungeon count
        mOptions[RSK_MQ_DUNGEON_COUNT].Set(mqSet);
        // handling set number and random number together
    } else if (mOptions[RSK_MQ_DUNGEON_RANDOM].IsNot(RO_MQ_DUNGEONS_NONE)) {
        // so we don't have to call this repeatedly
        uint8_t mqCount = mOptions[RSK_MQ_DUNGEON_COUNT].Get();
        // How many dungeons are set to MQ in selection
        uint8_t mqSet = 0;
        // the number of random
        uint8_t mqToSet = 0;
        // store the dungeons to randomly decide between. we use the id instead of a dungeon object to avoid a lot of
        // casting.
        std::vector<uint8_t> randMQOption = {};
        // if dungeons have been preset, process them
        if (mOptions[RSK_MQ_DUNGEON_SET]) {
            for (size_t i = 0; i < dungeons.size(); i++) {
                switch (mOptions[dungeons[i]->GetMQSetting()].Get()) {
                    case RO_MQ_SET_MQ:
                        dungeons[i]->SetMQ();
                        mqSet += 1;
                        break;
                    case RO_MQ_SET_RANDOM:
                        randMQOption.push_back(static_cast<uint8_t>(i));
                        dungeons[i]->SetDungeonKnown(false);
                        break;
                    default:
                        break;
                }
            }
            // otherwise, every dungeon is possible
        } else {
            // if count is MAX_MQ_DUNGEON_COUNT, we know everything is MQ, so can skip some setps and not set Known
            if (mOptions[RSK_MQ_DUNGEON_RANDOM].Is(RO_MQ_DUNGEONS_SET_NUMBER) && mqCount == MAX_MQ_DUNGEON_COUNT) {
                randMQOption.resize(MAX_MQ_DUNGEON_COUNT);
                for (int i = 0; i < MAX_MQ_DUNGEON_COUNT; i++) {
                    randMQOption[i] = i;
                }
                for (auto dungeon : dungeons) {
                    mOptions[dungeon->GetMQSetting()].Set(RO_MQ_SET_MQ);
                }
                // if it's fixed to zero, set it to None instead. the rest is processed after
            } else if (mOptions[RSK_MQ_DUNGEON_RANDOM].Is(RO_MQ_DUNGEONS_SET_NUMBER) && mqCount == 0) {
                mOptions[RSK_MQ_DUNGEON_RANDOM].Set(RO_MQ_DUNGEONS_NONE);
                // otherwise, make everything a possibility and unknown
            } else {
                for (size_t i = 0; i < dungeons.size(); i++) {
                    randMQOption.push_back(static_cast<uint8_t>(i));
                    dungeons[i]->SetDungeonKnown(false);
                    mOptions[dungeons[i]->GetMQSetting()].Set(RO_MQ_SET_RANDOM);
                }
            }
        }
        // if there's no random options, we can skip this
        if (randMQOption.size() > 0) {
            // Figure out how many dungeons to select, rolling the random number if needed
            if (mOptions[RSK_MQ_DUNGEON_RANDOM].Is(RO_MQ_DUNGEONS_RANDOM_NUMBER)) {
                mqToSet = Random(0, static_cast<int>(randMQOption.size()) + 1);
            } else if (mqCount > mqSet) {
                mqToSet = std::min(mqCount - mqSet, static_cast<int>(randMQOption.size()));
            }
            // we only need to shuffle if we're not using them all
            if (mqToSet <= static_cast<int8_t>(randMQOption.size()) && mqToSet > 0) {
                Shuffle(randMQOption);
            }
            for (uint8_t i = 0; i < mqToSet; i++) {
                dungeons[randMQOption[i]]->SetMQ();
            }
        } else {
            // if there's no random options, check if we can collapse the setting into None or Selection
            if (mqSet == 0) {
                mOptions[RSK_MQ_DUNGEON_RANDOM].Set(RO_MQ_DUNGEONS_NONE);
            } else {
                mOptions[RSK_MQ_DUNGEON_RANDOM].Set(RO_MQ_DUNGEONS_SELECTION);
            }
        }
        // reset the value set based on what was actually set
        mOptions[RSK_MQ_DUNGEON_COUNT].Set(mqToSet + mqSet);
    }
    // Not an if else as other settings can become None in processing
    if (mOptions[RSK_MQ_DUNGEON_RANDOM].Is(RO_MQ_DUNGEONS_NONE)) {
        mOptions[RSK_MQ_DUNGEON_SET].Set(RO_GENERIC_OFF);
        mOptions[RSK_MQ_DUNGEON_COUNT].Set(0);
        for (auto dungeon : dungeons) {
            mOptions[dungeon->GetMQSetting()].Set(RO_MQ_SET_VANILLA);
        }
    }

    // Set key ring for each dungeon
    for (const auto dungeon : dungeons) {
        dungeon->ClearKeyRing();
    }

    const std::vector<OptionValue*> keyRingOptions = {
        &mOptions[RSK_KEYRINGS_FOREST_TEMPLE], &mOptions[RSK_KEYRINGS_FIRE_TEMPLE],
        &mOptions[RSK_KEYRINGS_WATER_TEMPLE],  &mOptions[RSK_KEYRINGS_SPIRIT_TEMPLE],
        &mOptions[RSK_KEYRINGS_SHADOW_TEMPLE], &mOptions[RSK_KEYRINGS_BOTTOM_OF_THE_WELL],
        &mOptions[RSK_KEYRINGS_GTG],           &mOptions[RSK_KEYRINGS_GANONS_CASTLE],
    };

    if (mOptions[RSK_KEYRINGS]) {
        // Random Key Rings
        auto keyrings = keyRingOptions;
        if (mOptions[RSK_GERUDO_FORTRESS].Is(RO_GF_CARPENTERS_NORMAL) &&
            mOptions[RSK_GERUDO_KEYS].IsNot(RO_GERUDO_KEYS_VANILLA)) {
            keyrings.push_back(&mOptions[RSK_KEYRINGS_GERUDO_FORTRESS]);
        } else {
            mOptions[RSK_KEYRINGS_GERUDO_FORTRESS].Set(RO_KEYRING_FOR_DUNGEON_OFF);
        }
        if (mOptions[RSK_KEYRINGS].Is(RO_KEYRINGS_RANDOM) || mOptions[RSK_KEYRINGS].Is(RO_KEYRINGS_COUNT)) {
            const uint32_t keyRingCount = mOptions[RSK_KEYRINGS].Is(RO_KEYRINGS_COUNT)
                                              ? mOptions[RSK_KEYRINGS_RANDOM_COUNT].Get()
                                              : Random(0, static_cast<int>(keyrings.size()));
            Shuffle(keyrings);
            for (size_t i = 0; i < keyRingCount; i++) {
                keyrings[i]->Set(RO_KEYRING_FOR_DUNGEON_ON);
            }
            for (size_t i = keyRingCount; i < keyrings.size(); i++) {
                keyrings[i]->Set(RO_KEYRING_FOR_DUNGEON_OFF);
            }
        }
        if (mOptions[RSK_KEYRINGS_BOTTOM_OF_THE_WELL].Is(RO_KEYRING_FOR_DUNGEON_ON) ||
            (mOptions[RSK_KEYRINGS_BOTTOM_OF_THE_WELL].Is(RO_KEYRING_FOR_DUNGEON_RANDOM) && Random(0, 2) == 0)) {
            this->GetDungeon(BOTTOM_OF_THE_WELL)->SetKeyRing();
        }
        if (mOptions[RSK_KEYRINGS_FOREST_TEMPLE].Is(RO_KEYRING_FOR_DUNGEON_ON) ||
            (mOptions[RSK_KEYRINGS_FOREST_TEMPLE].Is(RO_KEYRING_FOR_DUNGEON_RANDOM) && Random(0, 2) == 0)) {
            this->GetDungeon(FOREST_TEMPLE)->SetKeyRing();
        }
        if (mOptions[RSK_KEYRINGS_FIRE_TEMPLE].Is(RO_KEYRING_FOR_DUNGEON_ON) ||
            (mOptions[RSK_KEYRINGS_FIRE_TEMPLE].Is(RO_KEYRING_FOR_DUNGEON_RANDOM) && Random(0, 2) == 0)) {
            this->GetDungeon(FIRE_TEMPLE)->SetKeyRing();
        }
        if (mOptions[RSK_KEYRINGS_WATER_TEMPLE].Is(RO_KEYRING_FOR_DUNGEON_ON) ||
            (mOptions[RSK_KEYRINGS_WATER_TEMPLE].Is(RO_KEYRING_FOR_DUNGEON_RANDOM) && Random(0, 2) == 0)) {
            this->GetDungeon(WATER_TEMPLE)->SetKeyRing();
        }
        if (mOptions[RSK_KEYRINGS_SPIRIT_TEMPLE].Is(RO_KEYRING_FOR_DUNGEON_ON) ||
            (mOptions[RSK_KEYRINGS_SPIRIT_TEMPLE].Is(RO_KEYRING_FOR_DUNGEON_RANDOM) && Random(0, 2) == 0)) {
            this->GetDungeon(SPIRIT_TEMPLE)->SetKeyRing();
        }
        if (mOptions[RSK_KEYRINGS_SHADOW_TEMPLE].Is(RO_KEYRING_FOR_DUNGEON_ON) ||
            (mOptions[RSK_KEYRINGS_SHADOW_TEMPLE].Is(RO_KEYRING_FOR_DUNGEON_RANDOM) && Random(0, 2) == 0)) {
            this->GetDungeon(SHADOW_TEMPLE)->SetKeyRing();
        }
        if (mOptions[RSK_KEYRINGS_GTG].Is(RO_KEYRING_FOR_DUNGEON_ON) ||
            (mOptions[RSK_KEYRINGS_GTG].Is(RO_KEYRING_FOR_DUNGEON_RANDOM) && Random(0, 2) == 0)) {
            this->GetDungeon(GERUDO_TRAINING_GROUND)->SetKeyRing();
        }
        if (mOptions[RSK_KEYRINGS_GANONS_CASTLE].Is(RO_KEYRING_FOR_DUNGEON_ON) ||
            (mOptions[RSK_KEYRINGS_GANONS_CASTLE].Is(RO_KEYRING_FOR_DUNGEON_RANDOM) && Random(0, 2) == 0)) {
            this->GetDungeon(GANONS_CASTLE)->SetKeyRing();
        }
    }

    auto trials = this->GetTrials()->GetTrialList();
    Shuffle(trials);
    for (const auto trial : trials) {
        trial->SetAsSkipped();
    }
    if (mOptions[RSK_GANONS_TRIALS].Is(RO_GANONS_TRIALS_SKIP)) {
        mOptions[RSK_TRIAL_COUNT].Set(0);
    } else if (mOptions[RSK_GANONS_TRIALS].Is(RO_GANONS_TRIALS_RANDOM_NUMBER)) {
        mOptions[RSK_TRIAL_COUNT].Set(
            Random(0, static_cast<int>(Rando::Settings::GetInstance()->GetOption(RSK_TRIAL_COUNT).GetOptionCount())));
    }
    for (uint8_t i = 0; i < mOptions[RSK_TRIAL_COUNT].Get(); i++) {
        trials[i]->SetAsRequired();
    }

    bool dungeonShuffle = !mOptions[RSK_SHUFFLE_DUNGEON_ENTRANCES].Is(RO_GENERIC_OFF);
    bool bossShuffle = !mOptions[RSK_SHUFFLE_BOSS_ENTRANCES].Is(RO_GENERIC_OFF);
    bool overworldShuffle = !mOptions[RSK_SHUFFLE_OVERWORLD_ENTRANCES].Is(RO_GENERIC_OFF);
    bool interiorShuffle = !mOptions[RSK_SHUFFLE_INTERIOR_ENTRANCES].Is(RO_INTERIOR_ENTRANCE_SHUFFLE_OFF);
    bool gerudoFortressShuffle = !mOptions[RSK_SHUFFLE_THIEVES_HIDEOUT_ENTRANCES].Is(RO_GENERIC_OFF);
    bool grottoShuffle = !mOptions[RSK_SHUFFLE_GROTTO_ENTRANCES].Is(RO_GENERIC_OFF);

    if (dungeonShuffle + bossShuffle + overworldShuffle + interiorShuffle + grottoShuffle <= 1) {
        mOptions[RSK_MIXED_ENTRANCE_POOLS].Set(RO_GENERIC_OFF);
    }

    if (!mOptions[RSK_MIXED_ENTRANCE_POOLS] || !dungeonShuffle) {
        mOptions[RSK_MIX_DUNGEON_ENTRANCES].Set(RO_GENERIC_OFF);
    }

    if (!mOptions[RSK_MIXED_ENTRANCE_POOLS] || !bossShuffle) {
        mOptions[RSK_MIX_BOSS_ENTRANCES].Set(RO_GENERIC_OFF);
    }

    if (!mOptions[RSK_MIXED_ENTRANCE_POOLS] || !overworldShuffle) {
        mOptions[RSK_MIX_OVERWORLD_ENTRANCES].Set(RO_GENERIC_OFF);
    }

    if (!mOptions[RSK_MIXED_ENTRANCE_POOLS] || !interiorShuffle) {
        mOptions[RSK_MIX_INTERIOR_ENTRANCES].Set(RO_GENERIC_OFF);
    }

    if (!mOptions[RSK_MIXED_ENTRANCE_POOLS] || !gerudoFortressShuffle) {
        mOptions[RSK_MIX_THIEVES_HIDEOUT_ENTRANCES].Set(RO_GENERIC_OFF);
    }

    if (!mOptions[RSK_MIXED_ENTRANCE_POOLS] || !grottoShuffle) {
        mOptions[RSK_MIX_GROTTO_ENTRANCES].Set(RO_GENERIC_OFF);
    }

    if (mOptions[RSK_STARTING_AGE].Is(RO_AGE_RANDOM)) {
        if (const uint32_t choice = Random(0, 2); choice == 0) {
            mOptions[RSK_SELECTED_STARTING_AGE].Set(RO_AGE_CHILD);
        } else {
            mOptions[RSK_SELECTED_STARTING_AGE].Set(RO_AGE_ADULT);
        }
    } else {
        mOptions[RSK_SELECTED_STARTING_AGE].Set(mOptions[RSK_STARTING_AGE].Get());
    }

    // TODO: Random Starting Time

    if (mOptions[RSK_GANONS_BOSS_KEY].Is(RO_GANON_BOSS_KEY_LACS_STONES)) {
        mLACSCondition = RO_LACS_STONES;
    } else if (mOptions[RSK_GANONS_BOSS_KEY].Is(RO_GANON_BOSS_KEY_LACS_MEDALLIONS)) {
        mLACSCondition = RO_LACS_MEDALLIONS;
    } else if (mOptions[RSK_GANONS_BOSS_KEY].Is(RO_GANON_BOSS_KEY_LACS_REWARDS)) {
        mLACSCondition = RO_LACS_REWARDS;
    } else if (mOptions[RSK_GANONS_BOSS_KEY].Is(RO_GANON_BOSS_KEY_LACS_DUNGEONS)) {
        mLACSCondition = RO_LACS_DUNGEONS;
    } else if (mOptions[RSK_GANONS_BOSS_KEY].Is(RO_GANON_BOSS_KEY_LACS_TOKENS)) {
        mLACSCondition = RO_LACS_TOKENS;
    } else {
        mLACSCondition = RO_LACS_VANILLA;
    }

    if (!mOptions[RSK_SHUFFLE_WARP_SONGS]) {
        mOptions[RSK_WARP_SONG_HINTS].Set(RO_GENERIC_OFF);
    }

    if (!mOptions[RSK_SHUFFLE_COWS]) {
        mOptions[RSK_MALON_HINT].Set(RO_GENERIC_OFF);
    }

    if (!mOptions[RSK_SHUFFLE_100_GS_REWARD]) {
        mOptions[RSK_KAK_100_SKULLS_HINT].Set(RO_GENERIC_OFF);
    }

    if (!mOptions[RSK_SHUFFLE_FISHING_POLE]) {
        mOptions[RSK_FISHING_POLE_HINT].Set(RO_GENERIC_OFF);
    }

    if (mOptions[RSK_FISHSANITY].IsNot(RO_FISHSANITY_HYRULE_LOACH)) {
        mOptions[RSK_LOACH_HINT].Set(RO_GENERIC_OFF);
    }
}

void Settings::ParseJson(nlohmann::json spoilerFileJson) {
    mContext->SetSeedString(spoilerFileJson["seed"].get<std::string>());
    mContext->SetSeed(spoilerFileJson["finalSeed"].get<uint32_t>());
    nlohmann::json settingsJson = spoilerFileJson["settings"];
    for (auto it = settingsJson.begin(); it != settingsJson.end(); ++it) {
        // todo load into cvars for UI
        // RANDOTODO handle numeric value to options conversion better than brute force
        if (StaticData::optionNameToEnum.contains(it.key())) {
            const RandomizerSettingKey index = StaticData::optionNameToEnum[it.key()];
            mContext->GetOption(index).Set(mOptions[index].GetValueFromText(it.value()));
        }
    }

    nlohmann::json jsonExcludedLocations = spoilerFileJson["excludedLocations"];
    const auto ctx = Context::GetInstance();

    for (auto it = jsonExcludedLocations.begin(); it != jsonExcludedLocations.end(); ++it) {
        const RandomizerCheck rc = Rando::StaticData::locationNameToEnum[it.value()];
        ctx->GetItemLocation(rc)->SetExcludedOption(RO_GENERIC_ON);
    }

    nlohmann::json enabledTricksJson = spoilerFileJson["enabledTricks"];
    for (auto it = enabledTricksJson.begin(); it != enabledTricksJson.end(); ++it) {
        const RandomizerTrick rt = mTrickNameToEnum[it.value()];
        GetTrickSetting(rt).SetContextIndex(RO_GENERIC_ON);
    }
}

void Settings::AssignContext(std::shared_ptr<Context> ctx) {
    mContext = ctx;
}

void Settings::ClearContext() {
    mContext = nullptr;
}

void Settings::SetAllToContext() {
    for (int i = 0; i < RSK_MAX; i++) {
        mContext->GetOption(static_cast<RandomizerSettingKey>(i)).Set(mOptions[i].GetOptionIndex());
    }
    for (int i = 0; i < RT_MAX; i++) {
        mContext->GetTrickOption(static_cast<RandomizerTrick>(i)).Set(mTrickSettings[i].GetOptionIndex());
    }
    for (int i = 0; i < RC_MAX; i++) {
        mContext->GetItemLocation(i)->SetExcludedOption(
            StaticData::GetLocation(static_cast<RandomizerCheck>(i))->GetExcludedOption()->GetOptionIndex());
    }
}

void Settings::RandomizeAllSettings() {
    // Randomize all settings except tricks
    for (int i = 0; i < RSK_MAX; i++) {
        switch (static_cast<RandomizerSettingKey>(i)) {
            case RSK_STARTING_SKULLTULA_TOKEN:
            case RSK_STARTING_HEARTS:
            case RSK_STARTING_ZELDAS_LULLABY:
            case RSK_STARTING_EPONAS_SONG:
            case RSK_STARTING_SARIAS_SONG:
            case RSK_STARTING_SUNS_SONG:
            case RSK_STARTING_SONG_OF_TIME:
            case RSK_STARTING_SONG_OF_STORMS:
            case RSK_STARTING_MINUET_OF_FOREST:
            case RSK_STARTING_BOLERO_OF_FIRE:
            case RSK_STARTING_SERENADE_OF_WATER:
            case RSK_STARTING_REQUIEM_OF_SPIRIT:
            case RSK_STARTING_NOCTURNE_OF_SHADOW:
            case RSK_STARTING_PRELUDE_OF_LIGHT:
                continue;
            default:
                break;
        }

        auto key = static_cast<RandomizerSettingKey>(i);
        Option& option = mOptions[key];

        if (option.GetOptionCount() == 0) {
            continue;
        }

        uint8_t randomIndex = Random(0, static_cast<uint32_t>(option.GetOptionCount()));

        option.SetContextIndex(randomIndex);
        if (!option.GetCVarName().empty()) {
            CVarSetInteger(option.GetCVarName().c_str(), randomIndex);
        }
        option.RunCallback();
    }

    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

std::shared_ptr<Settings> Settings::GetInstance() {
    if (mInstance == nullptr) {
        mInstance = std::make_shared<Settings>();
    }
    return mInstance;
}

// THE holder that kept every run's rando Context alive.
//
// `mInstance` is a process-lifetime static and `mContext` is a strong reference to the Context of
// whichever run last called AssignContext -- so the seed context could not die no matter what else
// released it. This was found by ordering the OTRGlobals free BEFORE the rando-context reset, which
// turned that reset from a workaround into a check: it kept reporting "STILL LIVE" after its supposed
// only owner had been freed, and after the Context <-> Logic cycle was broken as well.
//
// The whole Settings object is dropped, not just mContext: it holds the seed's options, trick options
// and excluded locations, all of which belong to the run that generated them. GetInstance rebuilds it
// on demand, and OTRGlobals::Initialize re-assigns the context every run.
void Settings::ResetRunState() {
    const bool inheritedSettings = (mInstance != nullptr);
    const bool inheritedContext = inheritedSettings && (mInstance->mContext != nullptr);

    mInstance.reset();

    // Both flags printed, because they fail differently: settings without a context is a run that
    // opened the rando menu, and settings WITH one is a run that was about to be handed the previous
    // run's seed.
    fprintf(stderr, "ZELDA3D CORE: rando settings reset -- previous run left settings=%s context=%s.\n",
            inheritedSettings ? "yes" : "no", inheritedContext ? "YES (its seed would have been reused)" : "no");
    fflush(stderr);
}

extern "C" void Zelda3D_RandoSettingsResetRunState(void) {
    Settings::ResetRunState();
}
} // namespace Rando
