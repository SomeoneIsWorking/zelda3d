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

void Settings::CreateOptionGroups() {
    mOptionGroups[RSG_LOGIC] = OptionGroup::SubGroup("Logic Options", {
                                                                          &mOptions[RSK_LOGIC_RULES],
                                                                          &mOptions[RSK_ALL_LOCATIONS_REACHABLE],
                                                                          &mOptions[RSK_SKULLS_SUNS_SONG],
                                                                          &mOptions[RSK_BIG_POE_COUNT],
                                                                      });
    // TODO: Exclude Locations Menus
    mTricksByArea.clear();
    std::vector<Option*> tricksOption;
    tricksOption.reserve(mTrickSettings.size());
    for (int i = 0; i < RT_MAX; i++) {
        auto trick = &mTrickSettings[i];
        if (!trick->GetName().empty()) {
            tricksOption.push_back(trick);
            mTrickNameToEnum[std::string(trick->GetName())] = static_cast<RandomizerTrick>(i);
            mTricksByArea[trick->GetArea()].push_back(static_cast<RandomizerTrick>(i));
        }
    }
    mOptionGroups[RSG_TRICKS] = OptionGroup::SubGroup("Logical Tricks", tricksOption);
    mOptionGroups[RSG_MENU_SECTION_LOGIC] = OptionGroup::SubGroup("Logic",
                                                                  {
                                                                      &mOptions[RSK_LOGIC_RULES],
                                                                      &mOptions[RSK_ALL_LOCATIONS_REACHABLE],
                                                                      &mOptions[RSK_STARTING_AGE],
                                                                      &mOptions[RSK_SKULLS_SUNS_SONG],
                                                                      &mOptions[RSK_BIG_POE_COUNT],
                                                                      &mOptions[RSK_BLUE_FIRE_ARROWS],
                                                                      &mOptions[RSK_SUNLIGHT_ARROWS],
                                                                      &mOptions[RSK_FULL_WALLETS],
                                                                      &mOptions[RSK_SLINGBOW_BREAK_BEEHIVES],
                                                                      &mOptions[RSK_SWORDLESS_EPONA_ITEMS],
                                                                      &mOptions[RSK_SKIP_CHILD_ZELDA],
                                                                      &mOptions[RSK_MASK_QUEST],
                                                                      &mOptions[RSK_SKIP_CHILD_STEALTH],
                                                                      &mOptions[RSK_EARLY_GRANNYS_SHOP],
                                                                      &mOptions[RSK_SKIP_PLANTING_BEANS],
                                                                      &mOptions[RSK_SKIP_EPONA_RACE],
                                                                      &mOptions[RSK_SKIP_SCARECROWS_SONG],
                                                                  },
                                                                  WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_SECTION_WINCON] = OptionGroup::SubGroup(
        "Win Condition",
        { &mOptions[RSK_TRIFORCE_HUNT], &mOptions[RSK_TRIFORCE_HUNT_PIECES_TOTAL],
          &mOptions[RSK_TRIFORCE_HUNT_PIECES_REQUIRED], &mOptions[RSK_TRIFORCE_HUNT_PIECES_LOCATION],
          &mOptions[RSK_GANONS_BOSS_KEY], &mOptions[RSK_LACS_OPTIONS], &mOptions[RSK_LACS_MEDALLION_COUNT],
          &mOptions[RSK_LACS_STONE_COUNT], &mOptions[RSK_LACS_DUNGEON_COUNT], &mOptions[RSK_LACS_REWARD_COUNT],
          &mOptions[RSK_LACS_TOKEN_COUNT] },
        WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_COLUMN_LOGIC_WINCON] = OptionGroup::SubGroup("",
                                                                        std::initializer_list<OptionGroup*>{
                                                                            &mOptionGroups[RSG_ITEM_POOL],
                                                                            &mOptionGroups[RSG_MENU_SECTION_LOGIC],
                                                                            &mOptionGroups[RSG_MENU_SECTION_WINCON],
                                                                        },
                                                                        WidgetContainerType::COLUMN);
    mOptionGroups[RSG_MENU_SECTION_AREA_ACCESS] =
        OptionGroup::SubGroup("Area Access",
                              {
                                  &mOptions[RSK_FOREST],
                                  &mOptions[RSK_KAK_GATE],
                                  &mOptions[RSK_DOOR_OF_TIME],
                                  &mOptions[RSK_ZORAS_FOUNTAIN],
                                  &mOptions[RSK_SLEEPING_WATERFALL],
                                  &mOptions[RSK_JABU_OPEN],
                                  &mOptions[RSK_LOCK_OVERWORLD_DOORS],
                                  &mOptions[RSK_GERUDO_FORTRESS],
                                  &mOptions[RSK_RAINBOW_BRIDGE],
                                  &mOptions[RSK_BRIDGE_OPTIONS],
                                  &mOptions[RSK_RAINBOW_BRIDGE_STONE_COUNT],
                                  &mOptions[RSK_RAINBOW_BRIDGE_MEDALLION_COUNT],
                                  &mOptions[RSK_RAINBOW_BRIDGE_REWARD_COUNT],
                                  &mOptions[RSK_RAINBOW_BRIDGE_DUNGEON_COUNT],
                                  &mOptions[RSK_RAINBOW_BRIDGE_TOKEN_COUNT],
                                  &mOptions[RSK_GANONS_TRIALS],
                                  &mOptions[RSK_TRIAL_COUNT],
                                  &mOptions[RSK_MEDALLION_LOCKED_TRIALS],
                              },
                              WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_COLUMN_AREA_ACCESS] =
        OptionGroup::SubGroup("", { &mOptionGroups[RSG_MENU_SECTION_AREA_ACCESS] }, WidgetContainerType::COLUMN);
    mOptionGroups[RSG_MENU_SECTION_ENTRANCES] = OptionGroup::SubGroup(
        "Entrances",
        { &mOptions[RSK_SHUFFLE_DUNGEON_ENTRANCES], &mOptions[RSK_SHUFFLE_BOSS_ENTRANCES],
          &mOptions[RSK_SHUFFLE_GANONS_TOWER_ENTRANCE], &mOptions[RSK_SHUFFLE_OVERWORLD_ENTRANCES],
          &mOptions[RSK_SHUFFLE_INTERIOR_ENTRANCES], &mOptions[RSK_SHUFFLE_THIEVES_HIDEOUT_ENTRANCES],
          &mOptions[RSK_SHUFFLE_GROTTO_ENTRANCES], &mOptions[RSK_SHUFFLE_OWL_DROPS], &mOptions[RSK_SHUFFLE_WARP_SONGS],
          &mOptions[RSK_SHUFFLE_OVERWORLD_SPAWNS], &mOptions[RSK_DECOUPLED_ENTRANCES],
          &mOptions[RSK_MIXED_ENTRANCE_POOLS], &mOptions[RSK_MIX_DUNGEON_ENTRANCES], &mOptions[RSK_MIX_BOSS_ENTRANCES],
          &mOptions[RSK_MIX_OVERWORLD_ENTRANCES], &mOptions[RSK_MIX_INTERIOR_ENTRANCES],
          &mOptions[RSK_MIX_THIEVES_HIDEOUT_ENTRANCES], &mOptions[RSK_MIX_GROTTO_ENTRANCES] },
        WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_COLUMN_ENTRANCES] =
        OptionGroup::SubGroup("", { &mOptionGroups[RSG_MENU_SECTION_ENTRANCES] }, WidgetContainerType::COLUMN);
    mOptionGroups[RSG_MENU_SIDEBAR_LOGIC_ACCESS] =
        OptionGroup::SubGroup("Logic/Access",
                              std::initializer_list<OptionGroup*>{ &mOptionGroups[RSG_MENU_COLUMN_LOGIC_WINCON],
                                                                   &mOptionGroups[RSG_MENU_COLUMN_AREA_ACCESS],
                                                                   &mOptionGroups[RSG_MENU_COLUMN_ENTRANCES] },
                              WidgetContainerType::TABLE);
    mOptionGroups[RSG_MENU_SECTION_DUNGEON_ITEMS] = OptionGroup::SubGroup("Dungeon Items",
                                                                          {
                                                                              &mOptions[RSK_SHUFFLE_MAPANDCOMPASS],
                                                                              &mOptions[RSK_KEYSANITY],
                                                                              &mOptions[RSK_BOSS_KEYSANITY],
                                                                              &mOptions[RSK_SHUFFLE_DUNGEON_REWARDS],
                                                                              &mOptions[RSK_GERUDO_KEYS],
                                                                              &mOptions[RSK_SHUFFLE_BOSS_SOULS],
                                                                          },
                                                                          WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_COLUMN_DUNGEON_ITEMS] =
        OptionGroup::SubGroup("", { &mOptionGroups[RSG_MENU_SECTION_DUNGEON_ITEMS] }, WidgetContainerType::COLUMN);
    mOptionGroups[RSG_MENU_SECTION_MQ] = OptionGroup::SubGroup("Master Quest",
                                                               {
                                                                   &mOptions[RSK_MQ_DUNGEON_RANDOM],
                                                                   &mOptions[RSK_MQ_DUNGEON_COUNT],
                                                                   &mOptions[RSK_MQ_DUNGEON_SET],
                                                                   &mOptions[RSK_MQ_DEKU_TREE],
                                                                   &mOptions[RSK_MQ_DODONGOS_CAVERN],
                                                                   &mOptions[RSK_MQ_JABU_JABU],
                                                                   &mOptions[RSK_MQ_FOREST_TEMPLE],
                                                                   &mOptions[RSK_MQ_FIRE_TEMPLE],
                                                                   &mOptions[RSK_MQ_WATER_TEMPLE],
                                                                   &mOptions[RSK_MQ_SPIRIT_TEMPLE],
                                                                   &mOptions[RSK_MQ_SHADOW_TEMPLE],
                                                                   &mOptions[RSK_MQ_BOTTOM_OF_THE_WELL],
                                                                   &mOptions[RSK_MQ_ICE_CAVERN],
                                                                   &mOptions[RSK_MQ_GTG],
                                                                   &mOptions[RSK_MQ_GANONS_CASTLE],
                                                               },
                                                               WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_COLUMN_MQ] =
        OptionGroup::SubGroup("", { &mOptionGroups[RSG_MENU_SECTION_MQ] }, WidgetContainerType::COLUMN);
    mOptionGroups[RSG_MENU_SECTION_KEYRINGS] = OptionGroup::SubGroup(
        "Keyrings",
        { &mOptions[RSK_KEYRINGS], &mOptions[RSK_KEYRINGS_RANDOM_COUNT], &mOptions[RSK_KEYRINGS_FOREST_TEMPLE],
          &mOptions[RSK_KEYRINGS_FIRE_TEMPLE], &mOptions[RSK_KEYRINGS_WATER_TEMPLE],
          &mOptions[RSK_KEYRINGS_SPIRIT_TEMPLE], &mOptions[RSK_KEYRINGS_SHADOW_TEMPLE],
          &mOptions[RSK_KEYRINGS_BOTTOM_OF_THE_WELL], &mOptions[RSK_KEYRINGS_GTG],
          &mOptions[RSK_KEYRINGS_GANONS_CASTLE], &mOptions[RSK_KEYRINGS_GERUDO_FORTRESS] },
        WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_COLUMN_KEYRINGS] =
        OptionGroup::SubGroup("", { &mOptionGroups[RSG_MENU_SECTION_KEYRINGS] }, WidgetContainerType::COLUMN);
    mOptionGroups[RSG_MENU_SIDEBAR_DUNGEONS] = OptionGroup::SubGroup("Dungeons",
                                                                     std::initializer_list<OptionGroup*>{
                                                                         &mOptionGroups[RSG_MENU_COLUMN_DUNGEON_ITEMS],
                                                                         &mOptionGroups[RSG_MENU_COLUMN_KEYRINGS],
                                                                         &mOptionGroups[RSG_MENU_COLUMN_MQ],
                                                                     },
                                                                     WidgetContainerType::TABLE);
    mOptionGroups[RSG_MENU_SECTION_BASIC_SHUFFLES] =
        OptionGroup::SubGroup("Shuffle Items",
                              {
                                  &mOptions[RSK_SHUFFLE_SONGS],
                                  &mOptions[RSK_SHUFFLE_TOKENS],
                                  &mOptions[RSK_SHUFFLE_KOKIRI_SWORD],
                                  &mOptions[RSK_SHUFFLE_MASTER_SWORD],
                                  &mOptions[RSK_SHUFFLE_OCARINA],
                                  &mOptions[RSK_SHUFFLE_WEIRD_EGG],
                                  &mOptions[RSK_SHUFFLE_GERUDO_MEMBERSHIP_CARD],
                                  &mOptions[RSK_FISHSANITY],
                                  &mOptions[RSK_FISHSANITY_POND_COUNT],
                                  &mOptions[RSK_FISHSANITY_AGE_SPLIT],
                                  &mOptions[RSK_SHUFFLE_FREESTANDING],
                                  &mOptions[RSK_SHUFFLE_WONDER_ITEMS],
                                  &mOptions[RSK_SHUFFLE_BEEHIVES],
                                  &mOptions[RSK_SHUFFLE_COWS],
                                  &mOptions[RSK_SHUFFLE_POTS],
                                  &mOptions[RSK_SHUFFLE_GRASS],
                                  &mOptions[RSK_SHUFFLE_CRATES],
                                  &mOptions[RSK_SHUFFLE_BOULDERS],
                                  &mOptions[RSK_SHUFFLE_ROCKS],
                                  &mOptions[RSK_SHUFFLE_TREES],
                                  &mOptions[RSK_SHUFFLE_BUSHES],
                                  &mOptions[RSK_SHUFFLE_ICICLES],
                                  &mOptions[RSK_SHUFFLE_RED_ICE],
                                  &mOptions[RSK_SHUFFLE_SIGNS],
                                  &mOptions[RSK_SHUFFLE_FROG_SONG_RUPEES],
                                  &mOptions[RSK_SHUFFLE_ADULT_TRADE],
                                  &mOptions[RSK_SHUFFLE_100_GS_REWARD],
                                  &mOptions[RSK_SHUFFLE_FOUNTAIN_FAIRIES],
                                  &mOptions[RSK_SHUFFLE_STONE_FAIRIES],
                                  &mOptions[RSK_SHUFFLE_BEAN_FAIRIES],
                                  &mOptions[RSK_SHUFFLE_SONG_FAIRIES],
                                  &mOptions[RSK_SHUFFLE_BUTTERFLY_FAIRIES],
                              },
                              WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_COLUMN_BASIC_SHUFFLES] =
        OptionGroup::SubGroup("", { &mOptionGroups[RSG_MENU_SECTION_BASIC_SHUFFLES] }, WidgetContainerType::COLUMN);
    mOptionGroups[RSG_MENU_SECTION_SHOP_SHUFFLES] =
        OptionGroup::SubGroup("Shuffle Shops & Merchants",
                              {
                                  &mOptions[RSK_SHOPSANITY],
                                  &mOptions[RSK_SHOPSANITY_COUNT],
                                  &mOptions[RSK_SHOPSANITY_PRICES],
                                  &mOptions[RSK_SHOPSANITY_PRICES_FIXED_PRICE],
                                  &mOptions[RSK_SHOPSANITY_PRICES_RANGE_1],
                                  &mOptions[RSK_SHOPSANITY_PRICES_RANGE_2],
                                  &mOptions[RSK_SHOPSANITY_PRICES_NO_WALLET_WEIGHT],
                                  &mOptions[RSK_SHOPSANITY_PRICES_CHILD_WALLET_WEIGHT],
                                  &mOptions[RSK_SHOPSANITY_PRICES_ADULT_WALLET_WEIGHT],
                                  &mOptions[RSK_SHOPSANITY_PRICES_GIANT_WALLET_WEIGHT],
                                  &mOptions[RSK_SHOPSANITY_PRICES_TYCOON_WALLET_WEIGHT],
                                  &mOptions[RSK_SHOPSANITY_PRICES_AFFORDABLE],
                                  &mOptions[RSK_SHOP_SHIELDS_AND_TUNICS_ONLY_REFILL],
                                  &mOptions[RSK_SHUFFLE_SCRUBS],
                                  &mOptions[RSK_SCRUBS_PRICES],
                                  &mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE],
                                  &mOptions[RSK_SCRUBS_PRICES_RANGE_1],
                                  &mOptions[RSK_SCRUBS_PRICES_RANGE_2],
                                  &mOptions[RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT],
                                  &mOptions[RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT],
                                  &mOptions[RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT],
                                  &mOptions[RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT],
                                  &mOptions[RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT],
                                  &mOptions[RSK_SCRUBS_PRICES_AFFORDABLE],
                                  &mOptions[RSK_SHUFFLE_MERCHANTS],
                                  &mOptions[RSK_MERCHANT_PRICES],
                                  &mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE],
                                  &mOptions[RSK_MERCHANT_PRICES_RANGE_1],
                                  &mOptions[RSK_MERCHANT_PRICES_RANGE_2],
                                  &mOptions[RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT],
                                  &mOptions[RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT],
                                  &mOptions[RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT],
                                  &mOptions[RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT],
                                  &mOptions[RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT],
                                  &mOptions[RSK_MERCHANT_PRICES_AFFORDABLE],
                                  &mOptions[RSK_SHUFFLE_BEGGAR],
                              },
                              WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_COLUMN_SHOP_SHUFFLES] =
        OptionGroup::SubGroup("", { &mOptionGroups[RSG_MENU_SECTION_SHOP_SHUFFLES] }, WidgetContainerType::COLUMN);
    mOptionGroups[RSG_MENU_SECTION_ADDITIONAL_ITEMS] = OptionGroup::SubGroup("Additional Items",
                                                                             {
                                                                                 &mOptions[RSK_SHUFFLE_CHILD_WALLET],
                                                                                 &mOptions[RSK_INCLUDE_TYCOON_WALLET],
                                                                                 &mOptions[RSK_SHUFFLE_FISHING_POLE],
                                                                                 &mOptions[RSK_SHUFFLE_DEKU_STICK_BAG],
                                                                                 &mOptions[RSK_SHUFFLE_DEKU_NUT_BAG],
                                                                                 &mOptions[RSK_SHUFFLE_OCARINA_BUTTONS],
                                                                                 &mOptions[RSK_SHUFFLE_SWIM],
                                                                                 &mOptions[RSK_SHUFFLE_GRAB],
                                                                                 &mOptions[RSK_SHUFFLE_CLIMB],
                                                                                 &mOptions[RSK_SHUFFLE_CRAWL],
                                                                                 &mOptions[RSK_SHUFFLE_SPEAK],
                                                                                 &mOptions[RSK_SHUFFLE_OPEN_CHEST],
                                                                                 &mOptions[RSK_SHUFFLE_BEAN_SOULS],
                                                                                 &mOptions[RSK_ROCS_FEATHER],
                                                                                 &mOptions[RSK_BOMBCHU_BAG],
                                                                                 &mOptions[RSK_ENABLE_BOMBCHU_DROPS],
                                                                                 &mOptions[RSK_INFINITE_UPGRADES],
                                                                                 &mOptions[RSK_SKELETON_KEY],
                                                                             },
                                                                             WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_COLUMN_ADDITIONAL_ITEMS] =
        OptionGroup::SubGroup("", { &mOptionGroups[RSG_MENU_SECTION_ADDITIONAL_ITEMS] }, WidgetContainerType::COLUMN);
    mOptionGroups[RSG_MENU_SIDEBAR_SHUFFLES] =
        OptionGroup::SubGroup("Shuffles",
                              {
                                  &mOptionGroups[RSG_MENU_COLUMN_BASIC_SHUFFLES],
                                  &mOptionGroups[RSG_MENU_COLUMN_SHOP_SHUFFLES],
                                  &mOptionGroups[RSG_MENU_COLUMN_ADDITIONAL_ITEMS],
                              },
                              WidgetContainerType::TABLE);
    mOptionGroups[RSG_MENU_SECTION_HINTS] = OptionGroup::SubGroup("Hints",
                                                                  {
                                                                      &mOptions[RSK_GOSSIP_STONE_HINTS],
                                                                      &mOptions[RSK_HINT_CLARITY],
                                                                      &mOptions[RSK_HINT_DISTRIBUTION],
                                                                  },
                                                                  WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_SECTION_TRAPS] = OptionGroup::SubGroup("Traps",
                                                                  {
                                                                      &mOptions[RSK_BASE_ICE_TRAPS],
                                                                      &mOptions[RSK_ADDITIONAL_ICE_TRAPS],
                                                                      &mOptions[RSK_ICE_TRAP_PERCENT],
                                                                  },
                                                                  WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_COLUMN_HINTS_TRAPS] =
        OptionGroup::SubGroup("",
                              std::initializer_list<OptionGroup*>{ &mOptionGroups[RSG_MENU_SECTION_HINTS],
                                                                   &mOptionGroups[RSG_MENU_SECTION_TRAPS] },
                              WidgetContainerType::COLUMN);
    mOptionGroups[RSG_MENU_SECTION_STATIC_HINTS] = OptionGroup::SubGroup(
        "Static Hints", { &mOptions[RSK_TOT_ALTAR_HINT],     &mOptions[RSK_GANONDORF_HINT],
                          &mOptions[RSK_SHEIK_LA_HINT],      &mOptions[RSK_BOSS_KEY_HINT],
                          &mOptions[RSK_DAMPES_DIARY_HINT],  &mOptions[RSK_GREG_HINT],
                          &mOptions[RSK_LOACH_HINT],         &mOptions[RSK_SARIA_HINT],
                          &mOptions[RSK_MIDO_HINT],          &mOptions[RSK_FROGS_HINT],
                          &mOptions[RSK_OOT_HINT],           &mOptions[RSK_BIGGORON_HINT],
                          &mOptions[RSK_BIG_POES_HINT],      &mOptions[RSK_CHICKENS_HINT],
                          &mOptions[RSK_MALON_HINT],         &mOptions[RSK_HBA_HINT],
                          &mOptions[RSK_FISHING_POLE_HINT],  &mOptions[RSK_WARP_SONG_HINTS],
                          &mOptions[RSK_SCRUB_TEXT_HINT],    &mOptions[RSK_MERCHANT_TEXT_HINT],
                          &mOptions[RSK_KAK_10_SKULLS_HINT], &mOptions[RSK_KAK_20_SKULLS_HINT],
                          &mOptions[RSK_KAK_30_SKULLS_HINT], &mOptions[RSK_KAK_40_SKULLS_HINT],
                          &mOptions[RSK_KAK_50_SKULLS_HINT], &mOptions[RSK_KAK_100_SKULLS_HINT],
                          &mOptions[RSK_MASK_SHOP_HINT] },
        WidgetContainerType::SECTION, "This setting adds some hints at locations other than Gossip Stones.");
    mOptionGroups[RSG_MENU_COLUMN_STATIC_HINTS] =
        OptionGroup::SubGroup("", { &mOptionGroups[RSG_MENU_SECTION_STATIC_HINTS] }, WidgetContainerType::COLUMN);
    mOptionGroups[RSG_MENU_SIDEBAR_HINTS_TRAPS] =
        OptionGroup::SubGroup("Hints/Traps",
                              std::initializer_list<OptionGroup*>{
                                  &mOptionGroups[RSG_MENU_COLUMN_HINTS_TRAPS],
                                  &mOptionGroups[RSG_MENU_COLUMN_STATIC_HINTS],
                              },
                              WidgetContainerType::TABLE);
    mOptionGroups[RSG_MENU_SECTION_STARTING_EQUIPS] = OptionGroup::SubGroup(
        "Equips",
        { &mOptions[RSK_LINKS_POCKET], &mOptions[RSK_LINKS_POCKET_REWARD], &mOptions[RSK_STARTING_KOKIRI_SWORD],
          &mOptions[RSK_STARTING_MASTER_SWORD], &mOptions[RSK_STARTING_DEKU_SHIELD] },
        WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_SECTION_STARTING_ITEMS] = OptionGroup::SubGroup("Items",
                                                                           {
                                                                               &mOptions[RSK_STARTING_OCARINA],
                                                                               &mOptions[RSK_STARTING_STICKS],
                                                                               &mOptions[RSK_STARTING_NUTS],
                                                                               &mOptions[RSK_STARTING_BEANS],
                                                                               &mOptions[RSK_STARTING_SKULLTULA_TOKEN],
                                                                               &mOptions[RSK_STARTING_HEARTS],
                                                                           },
                                                                           WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_COLUMN_STARTING_EQUIPMENT] =
        OptionGroup::SubGroup("",
                              std::initializer_list<OptionGroup*>{
                                  &mOptionGroups[RSG_MENU_SECTION_STARTING_EQUIPS],
                                  &mOptionGroups[RSG_MENU_SECTION_STARTING_ITEMS],
                              },
                              WidgetContainerType::COLUMN);
    mOptionGroups[RSG_MENU_SECTION_NORMAL_SONGS] = OptionGroup::SubGroup("Normal Songs",
                                                                         {
                                                                             &mOptions[RSK_STARTING_ZELDAS_LULLABY],
                                                                             &mOptions[RSK_STARTING_EPONAS_SONG],
                                                                             &mOptions[RSK_STARTING_SARIAS_SONG],
                                                                             &mOptions[RSK_STARTING_SUNS_SONG],
                                                                             &mOptions[RSK_STARTING_SONG_OF_TIME],
                                                                             &mOptions[RSK_STARTING_SONG_OF_STORMS],
                                                                         },
                                                                         WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_SECTION_WARP_SONGS] = OptionGroup::SubGroup("Warp Songs",
                                                                       {
                                                                           &mOptions[RSK_STARTING_MINUET_OF_FOREST],
                                                                           &mOptions[RSK_STARTING_BOLERO_OF_FIRE],
                                                                           &mOptions[RSK_STARTING_SERENADE_OF_WATER],
                                                                           &mOptions[RSK_STARTING_REQUIEM_OF_SPIRIT],
                                                                           &mOptions[RSK_STARTING_NOCTURNE_OF_SHADOW],
                                                                           &mOptions[RSK_STARTING_PRELUDE_OF_LIGHT],
                                                                       },
                                                                       WidgetContainerType::SECTION);
    mOptionGroups[RSG_MENU_COLUMN_STARTING_SONGS] =
        OptionGroup::SubGroup("",
                              std::initializer_list<OptionGroup*>{
                                  &mOptionGroups[RSG_MENU_SECTION_NORMAL_SONGS],
                                  &mOptionGroups[RSG_MENU_SECTION_WARP_SONGS],
                              },
                              WidgetContainerType::COLUMN);
    mOptionGroups[RSG_MENU_SIDEBAR_STARTING_ITEMS] =
        OptionGroup::SubGroup("Starting Items",
                              std::initializer_list<OptionGroup*>{
                                  &mOptionGroups[RSG_MENU_COLUMN_STARTING_EQUIPMENT],
                                  &mOptionGroups[RSG_MENU_COLUMN_STARTING_SONGS],
                              },
                              WidgetContainerType::TABLE);
    mOptionGroups[RSG_OPEN] = OptionGroup("Open Settings", {
                                                               &mOptions[RSK_FOREST],
                                                               &mOptions[RSK_KAK_GATE],
                                                               &mOptions[RSK_DOOR_OF_TIME],
                                                               &mOptions[RSK_ZORAS_FOUNTAIN],
                                                               &mOptions[RSK_SLEEPING_WATERFALL],
                                                               &mOptions[RSK_JABU_OPEN],
                                                               &mOptions[RSK_LOCK_OVERWORLD_DOORS],
                                                               &mOptions[RSK_GERUDO_FORTRESS],
                                                               &mOptions[RSK_RAINBOW_BRIDGE],
                                                               &mOptions[RSK_RAINBOW_BRIDGE_STONE_COUNT],
                                                               &mOptions[RSK_RAINBOW_BRIDGE_MEDALLION_COUNT],
                                                               &mOptions[RSK_RAINBOW_BRIDGE_REWARD_COUNT],
                                                               &mOptions[RSK_RAINBOW_BRIDGE_DUNGEON_COUNT],
                                                               &mOptions[RSK_RAINBOW_BRIDGE_TOKEN_COUNT],
                                                               &mOptions[RSK_BRIDGE_OPTIONS],
                                                               &mOptions[RSK_GANONS_TRIALS],
                                                               &mOptions[RSK_TRIAL_COUNT],
                                                               &mOptions[RSK_MEDALLION_LOCKED_TRIALS],
                                                           });
    mOptionGroups[RSG_WORLD] = OptionGroup("World Settings", {
                                                                 &mOptions[RSK_STARTING_AGE],
                                                                 &mOptions[RSK_SHUFFLE_ENTRANCES],
                                                                 &mOptions[RSK_SHUFFLE_DUNGEON_ENTRANCES],
                                                                 &mOptions[RSK_SHUFFLE_BOSS_ENTRANCES],
                                                                 &mOptions[RSK_SHUFFLE_GANONS_TOWER_ENTRANCE],
                                                                 &mOptions[RSK_SHUFFLE_OVERWORLD_ENTRANCES],
                                                                 &mOptions[RSK_SHUFFLE_INTERIOR_ENTRANCES],
                                                                 &mOptions[RSK_SHUFFLE_THIEVES_HIDEOUT_ENTRANCES],
                                                                 &mOptions[RSK_SHUFFLE_GROTTO_ENTRANCES],
                                                                 &mOptions[RSK_SHUFFLE_OWL_DROPS],
                                                                 &mOptions[RSK_SHUFFLE_WARP_SONGS],
                                                                 &mOptions[RSK_SHUFFLE_OVERWORLD_SPAWNS],
                                                                 &mOptions[RSK_MIXED_ENTRANCE_POOLS],
                                                                 &mOptions[RSK_MIX_DUNGEON_ENTRANCES],
                                                                 &mOptions[RSK_MIX_BOSS_ENTRANCES],
                                                                 &mOptions[RSK_MIX_OVERWORLD_ENTRANCES],
                                                                 &mOptions[RSK_MIX_INTERIOR_ENTRANCES],
                                                                 &mOptions[RSK_MIX_THIEVES_HIDEOUT_ENTRANCES],
                                                                 &mOptions[RSK_MIX_GROTTO_ENTRANCES],
                                                                 &mOptions[RSK_DECOUPLED_ENTRANCES],
                                                                 &mOptions[RSK_BOMBCHU_BAG],
                                                                 &mOptions[RSK_ENABLE_BOMBCHU_DROPS],
                                                                 &mOptions[RSK_TRIFORCE_HUNT],
                                                                 &mOptions[RSK_TRIFORCE_HUNT_PIECES_TOTAL],
                                                                 &mOptions[RSK_TRIFORCE_HUNT_PIECES_REQUIRED],
                                                                 &mOptions[RSK_TRIFORCE_HUNT_PIECES_LOCATION],
                                                                 &mOptions[RSK_MQ_DUNGEON_RANDOM],
                                                                 &mOptions[RSK_MQ_DUNGEON_COUNT],
                                                                 &mOptions[RSK_MQ_DUNGEON_SET],
                                                             });
    mOptionGroups[RSG_SHUFFLE_DUNGEON_QUESTS] = OptionGroup::SubGroup(
        "Shuffle Dungeon Quest",
        { &mOptions[RSK_MQ_DEKU_TREE], &mOptions[RSK_MQ_DODONGOS_CAVERN], &mOptions[RSK_MQ_JABU_JABU],
          &mOptions[RSK_MQ_FOREST_TEMPLE], &mOptions[RSK_MQ_FIRE_TEMPLE], &mOptions[RSK_MQ_WATER_TEMPLE],
          &mOptions[RSK_MQ_SPIRIT_TEMPLE], &mOptions[RSK_MQ_SHADOW_TEMPLE], &mOptions[RSK_MQ_BOTTOM_OF_THE_WELL],
          &mOptions[RSK_MQ_ICE_CAVERN], &mOptions[RSK_MQ_GTG], &mOptions[RSK_MQ_GANONS_CASTLE] });
    mOptionGroups[RSG_SHUFFLE] =
        OptionGroup("Shuffle Settings", {
                                            &mOptions[RSK_SHUFFLE_DUNGEON_REWARDS],
                                            &mOptions[RSK_LINKS_POCKET],
                                            &mOptions[RSK_SHUFFLE_SONGS],
                                            &mOptions[RSK_SHOPSANITY],
                                            &mOptions[RSK_SHOPSANITY_COUNT],
                                            &mOptions[RSK_SHOPSANITY_PRICES],
                                            &mOptions[RSK_SHOPSANITY_PRICES_FIXED_PRICE],
                                            &mOptions[RSK_SHOPSANITY_PRICES_RANGE_1],
                                            &mOptions[RSK_SHOPSANITY_PRICES_RANGE_2],
                                            &mOptions[RSK_SHOPSANITY_PRICES_NO_WALLET_WEIGHT],
                                            &mOptions[RSK_SHOPSANITY_PRICES_CHILD_WALLET_WEIGHT],
                                            &mOptions[RSK_SHOPSANITY_PRICES_ADULT_WALLET_WEIGHT],
                                            &mOptions[RSK_SHOPSANITY_PRICES_GIANT_WALLET_WEIGHT],
                                            &mOptions[RSK_SHOPSANITY_PRICES_TYCOON_WALLET_WEIGHT],
                                            &mOptions[RSK_SHOPSANITY_PRICES_AFFORDABLE],
                                            &mOptions[RSK_SHOP_SHIELDS_AND_TUNICS_ONLY_REFILL],
                                            &mOptions[RSK_FISHSANITY],
                                            &mOptions[RSK_FISHSANITY_POND_COUNT],
                                            &mOptions[RSK_FISHSANITY_AGE_SPLIT],
                                            &mOptions[RSK_SHUFFLE_FISHING_POLE],
                                            &mOptions[RSK_SHUFFLE_TOKENS],
                                            &mOptions[RSK_SHUFFLE_SCRUBS],
                                            &mOptions[RSK_SCRUBS_PRICES],
                                            &mOptions[RSK_SCRUBS_PRICES_FIXED_PRICE],
                                            &mOptions[RSK_SCRUBS_PRICES_RANGE_1],
                                            &mOptions[RSK_SCRUBS_PRICES_RANGE_2],
                                            &mOptions[RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT],
                                            &mOptions[RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT],
                                            &mOptions[RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT],
                                            &mOptions[RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT],
                                            &mOptions[RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT],
                                            &mOptions[RSK_SCRUBS_PRICES_AFFORDABLE],
                                            &mOptions[RSK_SHUFFLE_BEEHIVES],
                                            &mOptions[RSK_SHUFFLE_COWS],
                                            &mOptions[RSK_SHUFFLE_POTS],
                                            &mOptions[RSK_SHUFFLE_GRASS],
                                            &mOptions[RSK_SHUFFLE_CRATES],
                                            &mOptions[RSK_SHUFFLE_BOULDERS],
                                            &mOptions[RSK_SHUFFLE_ROCKS],
                                            &mOptions[RSK_SHUFFLE_TREES],
                                            &mOptions[RSK_SHUFFLE_BUSHES],
                                            &mOptions[RSK_SHUFFLE_ICICLES],
                                            &mOptions[RSK_SHUFFLE_RED_ICE],
                                            &mOptions[RSK_SHUFFLE_SIGNS],
                                            &mOptions[RSK_SHUFFLE_KOKIRI_SWORD],
                                            &mOptions[RSK_SHUFFLE_OCARINA],
                                            &mOptions[RSK_SHUFFLE_OCARINA_BUTTONS],
                                            &mOptions[RSK_SHUFFLE_SWIM],
                                            &mOptions[RSK_SHUFFLE_GRAB],
                                            &mOptions[RSK_SHUFFLE_CLIMB],
                                            &mOptions[RSK_SHUFFLE_CRAWL],
                                            &mOptions[RSK_SHUFFLE_SPEAK],
                                            &mOptions[RSK_SHUFFLE_OPEN_CHEST],
                                            &mOptions[RSK_SHUFFLE_WEIRD_EGG],
                                            &mOptions[RSK_SHUFFLE_GERUDO_MEMBERSHIP_CARD],
                                            &mOptions[RSK_SHUFFLE_MERCHANTS],
                                            &mOptions[RSK_MERCHANT_PRICES],
                                            &mOptions[RSK_MERCHANT_PRICES_FIXED_PRICE],
                                            &mOptions[RSK_MERCHANT_PRICES_RANGE_1],
                                            &mOptions[RSK_MERCHANT_PRICES_RANGE_2],
                                            &mOptions[RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT],
                                            &mOptions[RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT],
                                            &mOptions[RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT],
                                            &mOptions[RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT],
                                            &mOptions[RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT],
                                            &mOptions[RSK_MERCHANT_PRICES_AFFORDABLE],
                                            &mOptions[RSK_SHUFFLE_BEGGAR],
                                            &mOptions[RSK_SHUFFLE_FROG_SONG_RUPEES],
                                            &mOptions[RSK_SHUFFLE_ADULT_TRADE],
                                            &mOptions[RSK_SHUFFLE_CHEST_MINIGAME],
                                            &mOptions[RSK_SHUFFLE_100_GS_REWARD],
                                            &mOptions[RSK_SHUFFLE_BEAN_SOULS],
                                            &mOptions[RSK_ROCS_FEATHER],
                                            &mOptions[RSK_SHUFFLE_BOSS_SOULS],
                                            &mOptions[RSK_SHUFFLE_DEKU_STICK_BAG],
                                            &mOptions[RSK_SHUFFLE_DEKU_NUT_BAG],
                                            &mOptions[RSK_SHUFFLE_FREESTANDING],
                                            &mOptions[RSK_SHUFFLE_WONDER_ITEMS],
                                            &mOptions[RSK_SHUFFLE_FOUNTAIN_FAIRIES],
                                            &mOptions[RSK_SHUFFLE_STONE_FAIRIES],
                                            &mOptions[RSK_SHUFFLE_BEAN_FAIRIES],
                                            &mOptions[RSK_SHUFFLE_SONG_FAIRIES],
                                            &mOptions[RSK_SHUFFLE_BUTTERFLY_FAIRIES],
                                        });
    mOptionGroups[RSG_SHUFFLE_DUNGEON_ITEMS] =
        OptionGroup("Shuffle Dungeon Items", {
                                                 &mOptions[RSK_SHUFFLE_MAPANDCOMPASS],
                                                 &mOptions[RSK_KEYSANITY],
                                                 &mOptions[RSK_GERUDO_KEYS],
                                                 &mOptions[RSK_BOSS_KEYSANITY],
                                                 &mOptions[RSK_GANONS_BOSS_KEY],
                                                 &mOptions[RSK_LACS_STONE_COUNT],
                                                 &mOptions[RSK_LACS_MEDALLION_COUNT],
                                                 &mOptions[RSK_LACS_DUNGEON_COUNT],
                                                 &mOptions[RSK_LACS_REWARD_COUNT],
                                                 &mOptions[RSK_LACS_TOKEN_COUNT],
                                                 &mOptions[RSK_LACS_OPTIONS],
                                                 &mOptions[RSK_KEYRINGS],
                                                 &mOptions[RSK_KEYRINGS_RANDOM_COUNT],
                                                 &mOptions[RSK_KEYRINGS_GERUDO_FORTRESS],
                                                 &mOptions[RSK_KEYRINGS_FOREST_TEMPLE],
                                                 &mOptions[RSK_KEYRINGS_FIRE_TEMPLE],
                                                 &mOptions[RSK_KEYRINGS_WATER_TEMPLE],
                                                 &mOptions[RSK_KEYRINGS_SPIRIT_TEMPLE],
                                                 &mOptions[RSK_KEYRINGS_SHADOW_TEMPLE],
                                                 &mOptions[RSK_KEYRINGS_BOTTOM_OF_THE_WELL],
                                                 &mOptions[RSK_KEYRINGS_GTG],
                                                 &mOptions[RSK_KEYRINGS_GANONS_CASTLE],
                                             });
    mOptionGroups[RSG_STARTING_ITEMS] =
        OptionGroup::SubGroup("Items", { &mOptions[RSK_STARTING_OCARINA], &mOptions[RSK_STARTING_KOKIRI_SWORD],
                                         &mOptions[RSK_STARTING_DEKU_SHIELD] });
    mOptionGroups[RSG_STARTING_SONGS] =
        OptionGroup::SubGroup("Ocarina Songs", {
                                                   &mOptions[RSK_STARTING_ZELDAS_LULLABY],
                                                   &mOptions[RSK_STARTING_EPONAS_SONG],
                                                   &mOptions[RSK_STARTING_SARIAS_SONG],
                                                   &mOptions[RSK_STARTING_SUNS_SONG],
                                                   &mOptions[RSK_STARTING_SONG_OF_TIME],
                                                   &mOptions[RSK_STARTING_SONG_OF_STORMS],
                                                   &mOptions[RSK_STARTING_SONG_OF_TIME],
                                                   &mOptions[RSK_STARTING_MINUET_OF_FOREST],
                                                   &mOptions[RSK_STARTING_BOLERO_OF_FIRE],
                                                   &mOptions[RSK_STARTING_SERENADE_OF_WATER],
                                                   &mOptions[RSK_STARTING_REQUIEM_OF_SPIRIT],
                                                   &mOptions[RSK_STARTING_NOCTURNE_OF_SHADOW],
                                                   &mOptions[RSK_STARTING_PRELUDE_OF_LIGHT],
                                               });
    mOptionGroups[RSG_STARTING_OTHER] = OptionGroup::SubGroup("Other", {
                                                                           &mOptions[RSK_STARTING_STICKS],
                                                                           &mOptions[RSK_STARTING_NUTS],
                                                                           &mOptions[RSK_STARTING_BEANS],
                                                                           &mOptions[RSK_FULL_WALLETS],
                                                                           &mOptions[RSK_STARTING_SKULLTULA_TOKEN],
                                                                           &mOptions[RSK_STARTING_HEARTS],
                                                                       });
    mOptionGroups[RSG_STARTING_INVENTORY] = OptionGroup("Starting Inventory",
                                                        {
                                                            &mOptionGroups[RSG_STARTING_ITEMS],
                                                            &mOptionGroups[RSG_STARTING_SONGS],
                                                            &mOptionGroups[RSG_STARTING_OTHER],
                                                        },
                                                        OptionGroupType::DEFAULT);
    mOptionGroups[RSG_TIMESAVERS] = OptionGroup("Timesaver Settings", {
                                                                          &mOptions[RSK_SKIP_CHILD_ZELDA],
                                                                          &mOptions[RSK_SKIP_EPONA_RACE],
                                                                          &mOptions[RSK_SKIP_SCARECROWS_SONG],
                                                                          &mOptions[RSK_SKIP_PLANTING_BEANS],
                                                                          &mOptions[RSK_BIG_POE_COUNT],
                                                                      });
    mOptionGroups[RSG_MISC] = OptionGroup("Miscellaneous Settings",
                                          {
                                              &mOptions[RSK_GOSSIP_STONE_HINTS],
                                              &mOptions[RSK_HINT_CLARITY],
                                              &mOptions[RSK_HINT_DISTRIBUTION],
                                              &mOptions[RSK_TOT_ALTAR_HINT],
                                              &mOptions[RSK_GANONDORF_HINT],
                                              &mOptions[RSK_SHEIK_LA_HINT],
                                              &mOptions[RSK_BOSS_KEY_HINT],
                                              &mOptions[RSK_DAMPES_DIARY_HINT],
                                              &mOptions[RSK_GREG_HINT],
                                              &mOptions[RSK_LOACH_HINT],
                                              &mOptions[RSK_SARIA_HINT],
                                              &mOptions[RSK_MIDO_HINT],
                                              &mOptions[RSK_FROGS_HINT],
                                              &mOptions[RSK_OOT_HINT],
                                              &mOptions[RSK_WARP_SONG_HINTS],
                                              &mOptions[RSK_BIGGORON_HINT],
                                              &mOptions[RSK_BIG_POES_HINT],
                                              &mOptions[RSK_CHICKENS_HINT],
                                              &mOptions[RSK_MALON_HINT],
                                              &mOptions[RSK_HBA_HINT],
                                              &mOptions[RSK_KAK_10_SKULLS_HINT],
                                              &mOptions[RSK_KAK_20_SKULLS_HINT],
                                              &mOptions[RSK_KAK_30_SKULLS_HINT],
                                              &mOptions[RSK_KAK_40_SKULLS_HINT],
                                              &mOptions[RSK_KAK_50_SKULLS_HINT],
                                              &mOptions[RSK_KAK_100_SKULLS_HINT],
                                              &mOptions[RSK_MASK_SHOP_HINT],
                                              &mOptions[RSK_SCRUB_TEXT_HINT],
                                              &mOptions[RSK_MERCHANT_TEXT_HINT],
                                              &mOptions[RSK_FISHING_POLE_HINT],
                                              // TODO: Compasses show Reward/WOTH, Maps show Dungeon Mode, Starting Time
                                              &mOptions[RSK_DAMAGE_MULTIPLIER],
                                              &mOptions[RSK_BLUE_FIRE_ARROWS],
                                              &mOptions[RSK_SUNLIGHT_ARROWS],
                                              &mOptions[RSK_INFINITE_UPGRADES],
                                              &mOptions[RSK_SKELETON_KEY],
                                              &mOptions[RSK_SLINGBOW_BREAK_BEEHIVES],
                                          });
    mOptionGroups[RSG_ITEM_POOL] =
        OptionGroup("Item Pool Settings", std::initializer_list<Option*>({ &mOptions[RSK_ITEM_POOL] }));
    // TODO: Progressive Goron Sword, Remove Double Defense
    mOptionGroups[RSG_EXCLUDES_KOKIRI_FOREST] =
        OptionGroup::SubGroup("Kokiri Forest", mExcludeLocationsOptionsAreas[RCAREA_KOKIRI_FOREST]);
    mOptionGroups[RSG_EXCLUDES_LOST_WOODS] =
        OptionGroup::SubGroup("Lost Woods", mExcludeLocationsOptionsAreas[RCAREA_LOST_WOODS]);
    mOptionGroups[RSG_EXCLUDES_SACRED_FOREST_MEADOW] =
        OptionGroup::SubGroup("Sacred Forest Meadow", mExcludeLocationsOptionsAreas[RCAREA_SACRED_FOREST_MEADOW]);
    mOptionGroups[RSG_EXCLUDES_DEKU_TREE] =
        OptionGroup::SubGroup("Deku Tree", mExcludeLocationsOptionsAreas[RCAREA_DEKU_TREE]);
    mOptionGroups[RSG_EXCLUDES_FOREST_TEMPLE] =
        OptionGroup::SubGroup("Forest Temple", mExcludeLocationsOptionsAreas[RCAREA_FOREST_TEMPLE]);
    mOptionGroups[RSG_EXCLUDES_KAKARIKO_VILLAGE] =
        OptionGroup::SubGroup("Kakariko Village", mExcludeLocationsOptionsAreas[RCAREA_KAKARIKO_VILLAGE]);
    mOptionGroups[RSG_EXCLUDES_GRAVEYARD] =
        OptionGroup::SubGroup("Graveyard", mExcludeLocationsOptionsAreas[RCAREA_GRAVEYARD]);
    mOptionGroups[RSG_EXCLUDES_BOTTOM_OF_THE_WELL] =
        OptionGroup::SubGroup("Bottom of the Well", mExcludeLocationsOptionsAreas[RCAREA_BOTTOM_OF_THE_WELL]);
    mOptionGroups[RSG_EXCLUDES_SHADOW_TEMPLE] =
        OptionGroup::SubGroup("Shadow Temple", mExcludeLocationsOptionsAreas[RCAREA_SHADOW_TEMPLE]);
    mOptionGroups[RSG_EXCLUDES_DEATH_MOUNTAIN_TRAIL] =
        OptionGroup::SubGroup("Death Mountain Trail", mExcludeLocationsOptionsAreas[RCAREA_DEATH_MOUNTAIN_TRAIL]);
    mOptionGroups[RSG_EXCLUDES_DEATH_MOUNTAIN_CRATER] =
        OptionGroup::SubGroup("Death Mountain Crater", mExcludeLocationsOptionsAreas[RCAREA_DEATH_MOUNTAIN_CRATER]);
    mOptionGroups[RSG_EXCLUDES_GORON_CITY] =
        OptionGroup::SubGroup("Goron City", mExcludeLocationsOptionsAreas[RCAREA_GORON_CITY]);
    mOptionGroups[RSG_EXCLUDES_DODONGOS_CAVERN] =
        OptionGroup::SubGroup("Dodongo's Cavern", mExcludeLocationsOptionsAreas[RCAREA_DODONGOS_CAVERN]);
    mOptionGroups[RSG_EXCLUDES_FIRE_TEMPLE] =
        OptionGroup::SubGroup("Fire Temple", mExcludeLocationsOptionsAreas[RCAREA_FIRE_TEMPLE]);
    mOptionGroups[RSG_EXCLUDES_ZORAS_RIVER] =
        OptionGroup::SubGroup("Zora's River", mExcludeLocationsOptionsAreas[RCAREA_ZORAS_RIVER]);
    mOptionGroups[RSG_EXCLUDES_ZORAS_DOMAIN] =
        OptionGroup::SubGroup("Zora's Domain", mExcludeLocationsOptionsAreas[RCAREA_ZORAS_DOMAIN]);
    mOptionGroups[RSG_EXCLUDES_ZORAS_FOUNTAIN] =
        OptionGroup::SubGroup("Zora's Fountain", mExcludeLocationsOptionsAreas[RCAREA_ZORAS_FOUNTAIN]);
    mOptionGroups[RSG_EXCLUDES_JABU_JABU] =
        OptionGroup::SubGroup("Jabu Jabu's Belly", mExcludeLocationsOptionsAreas[RCAREA_JABU_JABUS_BELLY]);
    mOptionGroups[RSG_EXCLUDES_ICE_CAVERN] =
        OptionGroup::SubGroup("Ice Cavern", mExcludeLocationsOptionsAreas[RCAREA_ICE_CAVERN]);
    mOptionGroups[RSG_EXCLUDES_HYRULE_FIELD] =
        OptionGroup::SubGroup("Hyrule Field", mExcludeLocationsOptionsAreas[RCAREA_HYRULE_FIELD]);
    mOptionGroups[RSG_EXCLUDES_LON_LON_RANCH] =
        OptionGroup::SubGroup("Lon Lon Ranch", mExcludeLocationsOptionsAreas[RCAREA_LON_LON_RANCH]);
    mOptionGroups[RSG_EXCLUDES_LAKE_HYLIA] =
        OptionGroup::SubGroup("Lake Hylia", mExcludeLocationsOptionsAreas[RCAREA_LAKE_HYLIA]);
    mOptionGroups[RSG_EXCLUDES_WATER_TEMPLE] =
        OptionGroup::SubGroup("Water Temple", mExcludeLocationsOptionsAreas[RCAREA_WATER_TEMPLE]);
    mOptionGroups[RSG_EXCLUDES_GERUDO_VALLEY] =
        OptionGroup::SubGroup("Gerudo Valley", mExcludeLocationsOptionsAreas[RCAREA_GERUDO_VALLEY]);
    mOptionGroups[RSG_EXCLUDES_GERUDO_FORTRESS] =
        OptionGroup::SubGroup("Gerudo Fortress", mExcludeLocationsOptionsAreas[RCAREA_GERUDO_FORTRESS]);
    mOptionGroups[RSG_EXCLUDES_HAUNTED_WASTELAND] =
        OptionGroup::SubGroup("Haunted Wasteland", mExcludeLocationsOptionsAreas[RCAREA_WASTELAND]);
    mOptionGroups[RSG_EXCLUDES_DESERT_COLOSSUS] =
        OptionGroup::SubGroup("Desert Colossus", mExcludeLocationsOptionsAreas[RCAREA_DESERT_COLOSSUS]);
    mOptionGroups[RSG_EXCLUDES_GERUDO_TRAINING_GROUND] =
        OptionGroup::SubGroup("Gerudo Training Ground", mExcludeLocationsOptionsAreas[RCAREA_GERUDO_TRAINING_GROUND]);
    mOptionGroups[RSG_EXCLUDES_SPIRIT_TEMPLE] =
        OptionGroup::SubGroup("Spirit Temple", mExcludeLocationsOptionsAreas[RCAREA_SPIRIT_TEMPLE]);
    mOptionGroups[RSG_EXCLUDES_HYRULE_CASTLE] =
        OptionGroup::SubGroup("Hyrule Castle", mExcludeLocationsOptionsAreas[RCAREA_HYRULE_CASTLE]);
    mOptionGroups[RSG_EXCLUDES_MARKET] = OptionGroup::SubGroup("Market", mExcludeLocationsOptionsAreas[RCAREA_MARKET]);
    mOptionGroups[RSG_EXCLUDES_GANONS_CASTLE] =
        OptionGroup::SubGroup("Ganon's Castle", mExcludeLocationsOptionsAreas[RCAREA_GANONS_CASTLE]);
    mOptionGroups[RSG_EXCLUDES] =
        OptionGroup::SubGroup("Exclude Locations", {
                                                       &mOptionGroups[RSG_EXCLUDES_KOKIRI_FOREST],
                                                       &mOptionGroups[RSG_EXCLUDES_LOST_WOODS],
                                                       &mOptionGroups[RSG_EXCLUDES_SACRED_FOREST_MEADOW],
                                                       &mOptionGroups[RSG_EXCLUDES_DEKU_TREE],
                                                       &mOptionGroups[RSG_EXCLUDES_FOREST_TEMPLE],
                                                       &mOptionGroups[RSG_EXCLUDES_KAKARIKO_VILLAGE],
                                                       &mOptionGroups[RSG_EXCLUDES_GRAVEYARD],
                                                       &mOptionGroups[RSG_EXCLUDES_BOTTOM_OF_THE_WELL],
                                                       &mOptionGroups[RSG_EXCLUDES_SHADOW_TEMPLE],
                                                       &mOptionGroups[RSG_EXCLUDES_DEATH_MOUNTAIN_TRAIL],
                                                       &mOptionGroups[RSG_EXCLUDES_DEATH_MOUNTAIN_CRATER],
                                                       &mOptionGroups[RSG_EXCLUDES_GORON_CITY],
                                                       &mOptionGroups[RSG_EXCLUDES_DODONGOS_CAVERN],
                                                       &mOptionGroups[RSG_EXCLUDES_FIRE_TEMPLE],
                                                       &mOptionGroups[RSG_EXCLUDES_ZORAS_RIVER],
                                                       &mOptionGroups[RSG_EXCLUDES_ZORAS_DOMAIN],
                                                       &mOptionGroups[RSG_EXCLUDES_ZORAS_FOUNTAIN],
                                                       &mOptionGroups[RSG_EXCLUDES_JABU_JABU],
                                                       &mOptionGroups[RSG_EXCLUDES_ICE_CAVERN],
                                                       &mOptionGroups[RSG_EXCLUDES_HYRULE_FIELD],
                                                       &mOptionGroups[RSG_EXCLUDES_LON_LON_RANCH],
                                                       &mOptionGroups[RSG_EXCLUDES_LAKE_HYLIA],
                                                       &mOptionGroups[RSG_EXCLUDES_WATER_TEMPLE],
                                                       &mOptionGroups[RSG_EXCLUDES_GERUDO_VALLEY],
                                                       &mOptionGroups[RSG_EXCLUDES_GERUDO_FORTRESS],
                                                       &mOptionGroups[RSG_EXCLUDES_HAUNTED_WASTELAND],
                                                       &mOptionGroups[RSG_EXCLUDES_DESERT_COLOSSUS],
                                                       &mOptionGroups[RSG_EXCLUDES_GERUDO_TRAINING_GROUND],
                                                       &mOptionGroups[RSG_EXCLUDES_SPIRIT_TEMPLE],
                                                       &mOptionGroups[RSG_EXCLUDES_HYRULE_CASTLE],
                                                       &mOptionGroups[RSG_EXCLUDES_MARKET],
                                                       &mOptionGroups[RSG_EXCLUDES_GANONS_CASTLE],
                                                   });
    mOptionGroups[RSG_DETAILED_LOGIC] =
        OptionGroup("Detailed Logic Settings",
                    { &mOptionGroups[RSG_LOGIC], &mOptionGroups[RSG_TRICKS], &mOptionGroups[RSG_EXCLUDES] });
}

} // namespace Rando
