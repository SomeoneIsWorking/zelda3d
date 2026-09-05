#include "soh/OTRGlobals.h"
#include "logic.h"
#include "../debugger/performanceTimer.h"

#include <string>
#include <vector>

#include "dungeon.h"
#include "SeedContext.h"
#include "macros.h"
#include "variables.h"
#include <spdlog/spdlog.h>
#include <ship/utils/StringHelper.h>
#include "soh/resource/type/Scene.h"
#include "soh/resource/type/scenecommand/SetTransitionActorList.h"
#include "src/overlays/actors/ovl_En_Door/z_en_door.h"
#include "src/overlays/actors/ovl_Door_Shutter/z_door_shutter.h"
#include "location_access.h"

namespace Rando {

std::map<RandomizerGet, uint32_t> Logic::RandoGetToEquipFlag = {
    { RG_KOKIRI_SWORD, EQUIP_FLAG_SWORD_KOKIRI },       { RG_MASTER_SWORD, EQUIP_FLAG_SWORD_MASTER },
    { RG_GIANTS_KNIFE, EQUIP_FLAG_SWORD_BGS },          { RG_BIGGORON_SWORD, EQUIP_FLAG_SWORD_BGS },
    { RG_DEKU_SHIELD, EQUIP_FLAG_SHIELD_DEKU },         { RG_HYLIAN_SHIELD, EQUIP_FLAG_SHIELD_HYLIAN },
    { RG_MIRROR_SHIELD, EQUIP_FLAG_SHIELD_MIRROR },     { RG_GORON_TUNIC, EQUIP_FLAG_TUNIC_GORON },
    { RG_ZORA_TUNIC, EQUIP_FLAG_TUNIC_ZORA },           { RG_BUY_DEKU_SHIELD, EQUIP_FLAG_SHIELD_DEKU },
    { RG_BUY_HYLIAN_SHIELD, EQUIP_FLAG_SHIELD_HYLIAN }, { RG_BUY_GORON_TUNIC, EQUIP_FLAG_TUNIC_GORON },
    { RG_BUY_ZORA_TUNIC, EQUIP_FLAG_TUNIC_ZORA },       { RG_IRON_BOOTS, EQUIP_FLAG_BOOTS_IRON },
    { RG_HOVER_BOOTS, EQUIP_FLAG_BOOTS_HOVER }
};

std::map<RandomizerGet, uint32_t> Logic::RandoGetToRandInf = {
    { RG_ZELDAS_LETTER, RAND_INF_ZELDAS_LETTER },
    { RG_WEIRD_EGG, RAND_INF_WEIRD_EGG },
    { RG_RUTOS_LETTER, RAND_INF_OBTAINED_RUTOS_LETTER },
    { RG_DEATH_MOUNTAIN_CRATER_BEAN_SOUL, RAND_INF_DEATH_MOUNTAIN_CRATER_BEAN_SOUL },
    { RG_DEATH_MOUNTAIN_TRAIL_BEAN_SOUL, RAND_INF_DEATH_MOUNTAIN_TRAIL_BEAN_SOUL },
    { RG_DESERT_COLOSSUS_BEAN_SOUL, RAND_INF_DESERT_COLOSSUS_BEAN_SOUL },
    { RG_GERUDO_VALLEY_BEAN_SOUL, RAND_INF_GERUDO_VALLEY_BEAN_SOUL },
    { RG_GRAVEYARD_BEAN_SOUL, RAND_INF_GRAVEYARD_BEAN_SOUL },
    { RG_KOKIRI_FOREST_BEAN_SOUL, RAND_INF_KOKIRI_FOREST_BEAN_SOUL },
    { RG_LAKE_HYLIA_BEAN_SOUL, RAND_INF_LAKE_HYLIA_BEAN_SOUL },
    { RG_LOST_WOODS_BRIDGE_BEAN_SOUL, RAND_INF_LOST_WOODS_BRIDGE_BEAN_SOUL },
    { RG_LOST_WOODS_BEAN_SOUL, RAND_INF_LOST_WOODS_BEAN_SOUL },
    { RG_ZORAS_RIVER_BEAN_SOUL, RAND_INF_ZORAS_RIVER_BEAN_SOUL },
    { RG_GOHMA_SOUL, RAND_INF_GOHMA_SOUL },
    { RG_KING_DODONGO_SOUL, RAND_INF_KING_DODONGO_SOUL },
    { RG_BARINADE_SOUL, RAND_INF_BARINADE_SOUL },
    { RG_PHANTOM_GANON_SOUL, RAND_INF_PHANTOM_GANON_SOUL },
    { RG_VOLVAGIA_SOUL, RAND_INF_VOLVAGIA_SOUL },
    { RG_MORPHA_SOUL, RAND_INF_MORPHA_SOUL },
    { RG_BONGO_BONGO_SOUL, RAND_INF_BONGO_BONGO_SOUL },
    { RG_TWINROVA_SOUL, RAND_INF_TWINROVA_SOUL },
    { RG_GANON_SOUL, RAND_INF_GANON_SOUL },
    { RG_OCARINA_A_BUTTON, RAND_INF_HAS_OCARINA_A },
    { RG_OCARINA_C_UP_BUTTON, RAND_INF_HAS_OCARINA_C_UP },
    { RG_OCARINA_C_DOWN_BUTTON, RAND_INF_HAS_OCARINA_C_DOWN },
    { RG_OCARINA_C_LEFT_BUTTON, RAND_INF_HAS_OCARINA_C_LEFT },
    { RG_OCARINA_C_RIGHT_BUTTON, RAND_INF_HAS_OCARINA_C_RIGHT },
    { RG_KEATON_MASK, RAND_INF_CHILD_TRADES_HAS_MASK_KEATON },
    { RG_SKULL_MASK, RAND_INF_CHILD_TRADES_HAS_MASK_SKULL },
    { RG_SPOOKY_MASK, RAND_INF_CHILD_TRADES_HAS_MASK_SPOOKY },
    { RG_BUNNY_HOOD, RAND_INF_CHILD_TRADES_HAS_MASK_BUNNY },
    { RG_GORON_MASK, RAND_INF_CHILD_TRADES_HAS_MASK_GORON },
    { RG_ZORA_MASK, RAND_INF_CHILD_TRADES_HAS_MASK_ZORA },
    { RG_GERUDO_MASK, RAND_INF_CHILD_TRADES_HAS_MASK_GERUDO },
    { RG_MASK_OF_TRUTH, RAND_INF_CHILD_TRADES_HAS_MASK_TRUTH },
    { RG_SKELETON_KEY, RAND_INF_HAS_SKELETON_KEY },
    { RG_GREG_RUPEE, RAND_INF_GREG_FOUND },
    { RG_SPEAK_DEKU, RAND_INF_CAN_SPEAK_DEKU },
    { RG_SPEAK_GERUDO, RAND_INF_CAN_SPEAK_GERUDO },
    { RG_SPEAK_GORON, RAND_INF_CAN_SPEAK_GORON },
    { RG_SPEAK_HYLIAN, RAND_INF_CAN_SPEAK_HYLIAN },
    { RG_SPEAK_KOKIRI, RAND_INF_CAN_SPEAK_KOKIRI },
    { RG_SPEAK_ZORA, RAND_INF_CAN_SPEAK_ZORA },
    { RG_FISHING_POLE, RAND_INF_FISHING_POLE_FOUND },
    { RG_GUARD_HOUSE_KEY, RAND_INF_GUARD_HOUSE_KEY_OBTAINED },
    { RG_MARKET_BAZAAR_KEY, RAND_INF_MARKET_BAZAAR_KEY_OBTAINED },
    { RG_MARKET_POTION_SHOP_KEY, RAND_INF_MARKET_POTION_SHOP_KEY_OBTAINED },
    { RG_MASK_SHOP_KEY, RAND_INF_MASK_SHOP_KEY_OBTAINED },
    { RG_MARKET_SHOOTING_GALLERY_KEY, RAND_INF_MARKET_SHOOTING_GALLERY_KEY_OBTAINED },
    { RG_BOMBCHU_BOWLING_KEY, RAND_INF_BOMBCHU_BOWLING_KEY_OBTAINED },
    { RG_TREASURE_CHEST_GAME_BUILDING_KEY, RAND_INF_TREASURE_CHEST_GAME_BUILDING_KEY_OBTAINED },
    { RG_BOMBCHU_SHOP_KEY, RAND_INF_BOMBCHU_SHOP_KEY_OBTAINED },
    { RG_RICHARDS_HOUSE_KEY, RAND_INF_RICHARDS_HOUSE_KEY_OBTAINED },
    { RG_ALLEY_HOUSE_KEY, RAND_INF_ALLEY_HOUSE_KEY_OBTAINED },
    { RG_KAK_BAZAAR_KEY, RAND_INF_KAK_BAZAAR_KEY_OBTAINED },
    { RG_KAK_POTION_SHOP_KEY, RAND_INF_KAK_POTION_SHOP_KEY_OBTAINED },
    { RG_BOSS_HOUSE_KEY, RAND_INF_BOSS_HOUSE_KEY_OBTAINED },
    { RG_GRANNYS_POTION_SHOP_KEY, RAND_INF_GRANNYS_POTION_SHOP_KEY_OBTAINED },
    { RG_SKULLTULA_HOUSE_KEY, RAND_INF_SKULLTULA_HOUSE_KEY_OBTAINED },
    { RG_IMPAS_HOUSE_KEY, RAND_INF_IMPAS_HOUSE_KEY_OBTAINED },
    { RG_WINDMILL_KEY, RAND_INF_WINDMILL_KEY_OBTAINED },
    { RG_KAK_SHOOTING_GALLERY_KEY, RAND_INF_KAK_SHOOTING_GALLERY_KEY_OBTAINED },
    { RG_DAMPES_HUT_KEY, RAND_INF_DAMPES_HUT_KEY_OBTAINED },
    { RG_TALONS_HOUSE_KEY, RAND_INF_TALONS_HOUSE_KEY_OBTAINED },
    { RG_STABLES_KEY, RAND_INF_STABLES_KEY_OBTAINED },
    { RG_BACK_TOWER_KEY, RAND_INF_BACK_TOWER_KEY_OBTAINED },
    { RG_HYLIA_LAB_KEY, RAND_INF_HYLIA_LAB_KEY_OBTAINED },
    { RG_FISHING_HOLE_KEY, RAND_INF_FISHING_HOLE_KEY_OBTAINED },
};

std::map<uint32_t, uint32_t> Logic::RandoGetToDungeonScene = {
    { RG_FOREST_TEMPLE_SMALL_KEY, SCENE_FOREST_TEMPLE },
    { RG_FIRE_TEMPLE_SMALL_KEY, SCENE_FIRE_TEMPLE },
    { RG_WATER_TEMPLE_SMALL_KEY, SCENE_WATER_TEMPLE },
    { RG_SPIRIT_TEMPLE_SMALL_KEY, SCENE_SPIRIT_TEMPLE },
    { RG_SHADOW_TEMPLE_SMALL_KEY, SCENE_SHADOW_TEMPLE },
    { RG_BOTTOM_OF_THE_WELL_SMALL_KEY, SCENE_BOTTOM_OF_THE_WELL },
    { RG_GERUDO_TRAINING_GROUND_SMALL_KEY, SCENE_GERUDO_TRAINING_GROUND },
    { RG_GERUDO_FORTRESS_SMALL_KEY, SCENE_THIEVES_HIDEOUT },
    { RG_GANONS_CASTLE_SMALL_KEY, SCENE_INSIDE_GANONS_CASTLE },
    { RG_FOREST_TEMPLE_KEY_RING, SCENE_FOREST_TEMPLE },
    { RG_FIRE_TEMPLE_KEY_RING, SCENE_FIRE_TEMPLE },
    { RG_WATER_TEMPLE_KEY_RING, SCENE_WATER_TEMPLE },
    { RG_SPIRIT_TEMPLE_KEY_RING, SCENE_SPIRIT_TEMPLE },
    { RG_SHADOW_TEMPLE_KEY_RING, SCENE_SHADOW_TEMPLE },
    { RG_BOTTOM_OF_THE_WELL_KEY_RING, SCENE_BOTTOM_OF_THE_WELL },
    { RG_GERUDO_TRAINING_GROUND_KEY_RING, SCENE_GERUDO_TRAINING_GROUND },
    { RG_GERUDO_FORTRESS_KEY_RING, SCENE_THIEVES_HIDEOUT },
    { RG_GANONS_CASTLE_KEY_RING, SCENE_INSIDE_GANONS_CASTLE },
    { RG_FOREST_TEMPLE_BOSS_KEY, SCENE_FOREST_TEMPLE },
    { RG_FIRE_TEMPLE_BOSS_KEY, SCENE_FIRE_TEMPLE },
    { RG_WATER_TEMPLE_BOSS_KEY, SCENE_WATER_TEMPLE },
    { RG_SPIRIT_TEMPLE_BOSS_KEY, SCENE_SPIRIT_TEMPLE },
    { RG_SHADOW_TEMPLE_BOSS_KEY, SCENE_SHADOW_TEMPLE },
    { RG_GANONS_CASTLE_BOSS_KEY, SCENE_INSIDE_GANONS_CASTLE },
    { RG_DEKU_TREE_MAP, SCENE_DEKU_TREE },
    { RG_DODONGOS_CAVERN_MAP, SCENE_DODONGOS_CAVERN },
    { RG_JABU_JABUS_BELLY_MAP, SCENE_JABU_JABU },
    { RG_FOREST_TEMPLE_MAP, SCENE_FOREST_TEMPLE },
    { RG_FIRE_TEMPLE_MAP, SCENE_FIRE_TEMPLE },
    { RG_WATER_TEMPLE_MAP, SCENE_WATER_TEMPLE },
    { RG_SPIRIT_TEMPLE_MAP, SCENE_SPIRIT_TEMPLE },
    { RG_SHADOW_TEMPLE_MAP, SCENE_SHADOW_TEMPLE },
    { RG_BOTTOM_OF_THE_WELL_MAP, SCENE_BOTTOM_OF_THE_WELL },
    { RG_ICE_CAVERN_MAP, SCENE_ICE_CAVERN },
    { RG_DEKU_TREE_COMPASS, SCENE_DEKU_TREE },
    { RG_DODONGOS_CAVERN_COMPASS, SCENE_DODONGOS_CAVERN },
    { RG_JABU_JABUS_BELLY_COMPASS, SCENE_JABU_JABU },
    { RG_FOREST_TEMPLE_COMPASS, SCENE_FOREST_TEMPLE },
    { RG_FIRE_TEMPLE_COMPASS, SCENE_FIRE_TEMPLE },
    { RG_WATER_TEMPLE_COMPASS, SCENE_WATER_TEMPLE },
    { RG_SPIRIT_TEMPLE_COMPASS, SCENE_SPIRIT_TEMPLE },
    { RG_SHADOW_TEMPLE_COMPASS, SCENE_SHADOW_TEMPLE },
    { RG_BOTTOM_OF_THE_WELL_COMPASS, SCENE_BOTTOM_OF_THE_WELL },
    { RG_ICE_CAVERN_COMPASS, SCENE_ICE_CAVERN },
    { RG_TREASURE_GAME_SMALL_KEY, SCENE_TREASURE_BOX_SHOP }
};

std::map<uint32_t, uint32_t> Logic::RandoGetToQuestItem = {
    { RG_FOREST_MEDALLION, QUEST_MEDALLION_FOREST },
    { RG_FIRE_MEDALLION, QUEST_MEDALLION_FIRE },
    { RG_WATER_MEDALLION, QUEST_MEDALLION_WATER },
    { RG_SPIRIT_MEDALLION, QUEST_MEDALLION_SPIRIT },
    { RG_SHADOW_MEDALLION, QUEST_MEDALLION_SHADOW },
    { RG_LIGHT_MEDALLION, QUEST_MEDALLION_LIGHT },
    { RG_MINUET_OF_FOREST, QUEST_SONG_MINUET },
    { RG_BOLERO_OF_FIRE, QUEST_SONG_BOLERO },
    { RG_SERENADE_OF_WATER, QUEST_SONG_SERENADE },
    { RG_REQUIEM_OF_SPIRIT, QUEST_SONG_REQUIEM },
    { RG_NOCTURNE_OF_SHADOW, QUEST_SONG_NOCTURNE },
    { RG_PRELUDE_OF_LIGHT, QUEST_SONG_PRELUDE },
    { RG_ZELDAS_LULLABY, QUEST_SONG_LULLABY },
    { RG_EPONAS_SONG, QUEST_SONG_EPONA },
    { RG_SARIAS_SONG, QUEST_SONG_SARIA },
    { RG_SUNS_SONG, QUEST_SONG_SUN },
    { RG_SONG_OF_TIME, QUEST_SONG_TIME },
    { RG_SONG_OF_STORMS, QUEST_SONG_STORMS },
    { RG_KOKIRI_EMERALD, QUEST_KOKIRI_EMERALD },
    { RG_GORON_RUBY, QUEST_GORON_RUBY },
    { RG_ZORA_SAPPHIRE, QUEST_ZORA_SAPPHIRE },
    { RG_STONE_OF_AGONY, QUEST_STONE_OF_AGONY },
    { RG_GERUDO_MEMBERSHIP_CARD, QUEST_GERUDO_CARD },
};

std::map<uint32_t, uint32_t> BottleRandomizerGetToItemID = {
    { RG_BOTTLE_WITH_RED_POTION, ITEM_POTION_RED },
    { RG_BOTTLE_WITH_GREEN_POTION, ITEM_POTION_GREEN },
    { RG_BOTTLE_WITH_BLUE_POTION, ITEM_POTION_BLUE },
    { RG_BOTTLE_WITH_FAIRY, ITEM_FAIRY },
    { RG_BOTTLE_WITH_FISH, ITEM_FISH },
    { RG_BOTTLE_WITH_BLUE_FIRE, ITEM_BLUE_FIRE },
    { RG_BOTTLE_WITH_BUGS, ITEM_BUG },
    { RG_BOTTLE_WITH_POE, ITEM_POE },
    { RG_BOTTLE_WITH_BIG_POE, ITEM_BIG_POE },
};

uint32_t HookshotLookup[3] = { ITEM_NONE, ITEM_HOOKSHOT, ITEM_LONGSHOT };
uint32_t OcarinaLookup[3] = { ITEM_NONE, ITEM_OCARINA_FAIRY, ITEM_OCARINA_TIME };

std::set<RandomizerGet> StaticData::restrictFW = { RG_FARORES_WIND };

std::set<RandomizerGet> StaticData::restrictSpells = { RG_FARORES_WIND, RG_DINS_FIRE, RG_NAYRUS_LOVE };

std::set<RandomizerGet> StaticData::restrictTrade = {
    RG_POCKET_EGG,   RG_COJIRO,       RG_ODD_MUSHROOM, RG_ODD_POTION, RG_POACHERS_SAW,
    RG_BROKEN_SWORD, RG_PRESCRIPTION, RG_EYEBALL_FROG, RG_EYEDROPS,   RG_CLAIM_CHECK,
};

std::set<RandomizerGet> StaticData::allowMasks = {
    RG_KEATON_MASK, RG_SKULL_MASK,  RG_SPOOKY_MASK,   RG_BUNNY_HOOD, RG_GORON_MASK,
    RG_ZORA_MASK,   RG_GERUDO_MASK, RG_MASK_OF_TRUTH, RG_WEIRD_EGG,  RG_ZELDAS_LETTER,
};

std::set<RandomizerGet> StaticData::allowBottleMaskTrade = { RG_KEATON_MASK,
                                                             RG_SKULL_MASK,
                                                             RG_SPOOKY_MASK,
                                                             RG_BUNNY_HOOD,
                                                             RG_GORON_MASK,
                                                             RG_ZORA_MASK,
                                                             RG_GERUDO_MASK,
                                                             RG_MASK_OF_TRUTH,
                                                             RG_WEIRD_EGG,
                                                             RG_ZELDAS_LETTER,
                                                             RG_POCKET_EGG,
                                                             RG_COJIRO,
                                                             RG_ODD_MUSHROOM,
                                                             RG_ODD_POTION,
                                                             RG_POACHERS_SAW,
                                                             RG_BROKEN_SWORD,
                                                             RG_PRESCRIPTION,
                                                             RG_EYEBALL_FROG,
                                                             RG_EYEDROPS,
                                                             RG_CLAIM_CHECK,
                                                             RG_EMPTY_BOTTLE,
                                                             RG_BOTTLE_WITH_MILK,
                                                             RG_BOTTLE_WITH_RED_POTION,
                                                             RG_BOTTLE_WITH_GREEN_POTION,
                                                             RG_BOTTLE_WITH_BLUE_POTION,
                                                             RG_BOTTLE_WITH_FAIRY,
                                                             RG_BOTTLE_WITH_FISH,
                                                             RG_BOTTLE_WITH_BLUE_FIRE,
                                                             RG_BOTTLE_WITH_BUGS,
                                                             RG_BOTTLE_WITH_POE,
                                                             RG_RUTOS_LETTER,
                                                             RG_BOTTLE_WITH_BIG_POE };

void Logic::ApplyItemEffect(Item& item, bool state) {
    auto randoGet = item.GetRandomizerGet();
    if (item.GetGIEntry()->objectId == OBJECT_GI_STICK) {
        SetInventory(ITEM_STICK, (!state ? ITEM_NONE : ITEM_STICK));
    }
    if (item.GetGIEntry()->objectId == OBJECT_GI_NUTS) {
        SetInventory(ITEM_NUT, (!state ? ITEM_NONE : ITEM_NUT));
    }
    switch (item.GetItemType()) {
        case ITEMTYPE_ITEM: {
            switch (randoGet) {
                case RG_STONE_OF_AGONY:
                case RG_GERUDO_MEMBERSHIP_CARD:
                    SetQuestItem(RandoGetToQuestItem.at(randoGet), state);
                    break;
                case RG_WEIRD_EGG:
                    SetRandoInf(RAND_INF_WEIRD_EGG, state);
                    break;
                case RG_ZELDAS_LETTER:
                    SetRandoInf(RAND_INF_ZELDAS_LETTER, state);
                    break;
                case RG_DOUBLE_DEFENSE:
                    mSaveContext->isDoubleDefenseAcquired = state;
                    break;
                case RG_POCKET_EGG:
                    SetRandoInf(RAND_INF_ADULT_TRADES_HAS_POCKET_EGG, state);
                    break;
                case RG_COJIRO:
                case RG_ODD_MUSHROOM:
                case RG_ODD_POTION:
                case RG_POACHERS_SAW:
                case RG_BROKEN_SWORD:
                case RG_PRESCRIPTION:
                case RG_EYEBALL_FROG:
                case RG_EYEDROPS:
                case RG_CLAIM_CHECK:
                    SetRandoInf(randoGet - RG_COJIRO + RAND_INF_ADULT_TRADES_HAS_COJIRO, state);
                    break;
                case RG_CLIMB:
                    SetRandoInf(RAND_INF_CAN_CLIMB, state);
                    break;
                case RG_CRAWL:
                    SetRandoInf(RAND_INF_CAN_CRAWL, state);
                    break;
                case RG_OPEN_CHEST:
                    SetRandoInf(RAND_INF_CAN_OPEN_CHEST, state);
                    break;
                case RG_PROGRESSIVE_HOOKSHOT: {
                    uint8_t i;
                    for (i = 0; i < 3; i++) {
                        if (CurrentInventory(ITEM_HOOKSHOT) == HookshotLookup[i]) {
                            break;
                        }
                    }
                    auto newItem = i + (!state ? -1 : 1);
                    if (newItem < 0) {
                        newItem = 0;
                    } else if (newItem > 2) {
                        newItem = 2;
                    }
                    SetInventory(ITEM_HOOKSHOT, HookshotLookup[newItem]);
                } break;
                case RG_PROGRESSIVE_STRENGTH: {
                    auto currentLevel = CurrentUpgrade(UPG_STRENGTH);
                    if (!CheckRandoInf(RAND_INF_CAN_GRAB) && state) {
                        SetRandoInf(RAND_INF_CAN_GRAB, true);
                    } else if (currentLevel == 0 && !state) {
                        SetRandoInf(RAND_INF_CAN_GRAB, false);
                    } else {
                        auto newLevel = currentLevel + (!state ? -1 : 1);
                        SetUpgrade(UPG_STRENGTH, newLevel);
                    }
                } break;
                case RG_PROGRESSIVE_BOMB_BAG: {
                    auto realGI = item.GetGIEntry();
                    if (realGI->itemId == RG_BOMB_BAG_INF && realGI->modIndex == MOD_RANDOMIZER) {
                        SetRandoInf(RAND_INF_HAS_INFINITE_BOMB_BAG, true);
                        break;
                    }
                    auto currentLevel = CurrentUpgrade(UPG_BOMB_BAG);
                    auto newLevel = currentLevel + (!state ? -1 : 1);
                    if (currentLevel == 0 && state || currentLevel == 1 && !state) {
                        SetInventory(ITEM_BOMB, (!state ? ITEM_NONE : ITEM_BOMB));
                    }
                    SetUpgrade(UPG_BOMB_BAG, newLevel);
                } break;
                case RG_PROGRESSIVE_BOW: {
                    auto realGI = item.GetGIEntry();
                    if (realGI->itemId == RG_QUIVER_INF && realGI->modIndex == MOD_RANDOMIZER) {
                        SetRandoInf(RAND_INF_HAS_INFINITE_QUIVER, true);
                        break;
                    }
                    auto currentLevel = CurrentUpgrade(UPG_QUIVER);
                    auto newLevel = currentLevel + (!state ? -1 : 1);
                    if (currentLevel == 0 && state || currentLevel == 1 && !state) {
                        SetInventory(ITEM_BOW, (!state ? ITEM_NONE : ITEM_BOW));
                    }
                    SetUpgrade(UPG_QUIVER, newLevel);
                } break;
                case RG_PROGRESSIVE_SLINGSHOT: {
                    auto realGI = item.GetGIEntry();
                    if (realGI->itemId == RG_BULLET_BAG_INF && realGI->modIndex == MOD_RANDOMIZER) {
                        SetRandoInf(RAND_INF_HAS_INFINITE_BULLET_BAG, true);
                        break;
                    }
                    auto currentLevel = CurrentUpgrade(UPG_BULLET_BAG);
                    auto newLevel = currentLevel + (!state ? -1 : 1);
                    if (currentLevel == 0 && state || currentLevel == 1 && !state) {
                        SetInventory(ITEM_SLINGSHOT, (!state ? ITEM_NONE : ITEM_SLINGSHOT));
                    }
                    SetUpgrade(UPG_BULLET_BAG, newLevel);
                } break;
                case RG_PROGRESSIVE_WALLET: {
                    auto realGI = item.GetGIEntry();
                    if (realGI->itemId == RG_WALLET_INF && realGI->modIndex == MOD_RANDOMIZER) {
                        SetRandoInf(RAND_INF_HAS_INFINITE_MONEY, true);
                        break;
                    }
                    auto currentLevel = CurrentUpgrade(UPG_WALLET);
                    if (!CheckRandoInf(RAND_INF_HAS_WALLET) && state) {
                        SetRandoInf(RAND_INF_HAS_WALLET, true);
                    } else if (currentLevel == 0 && !state) {
                        SetRandoInf(RAND_INF_HAS_WALLET, false);
                    } else {
                        auto newLevel = currentLevel + (!state ? -1 : 1);
                        SetUpgrade(UPG_WALLET, newLevel);
                    }
                } break;
                case RG_PROGRESSIVE_SCALE: {
                    auto currentLevel = CurrentUpgrade(UPG_SCALE);
                    if (!CheckRandoInf(RAND_INF_CAN_SWIM) && state) {
                        SetRandoInf(RAND_INF_CAN_SWIM, true);
                    } else if (currentLevel == 0 && !state) {
                        SetRandoInf(RAND_INF_CAN_SWIM, false);
                    } else {
                        auto newLevel = currentLevel + (!state ? -1 : 1);
                        SetUpgrade(UPG_SCALE, newLevel);
                    }
                } break;
                case RG_PROGRESSIVE_NUT_UPGRADE: {
                    auto realGI = item.GetGIEntry();
                    if (realGI->itemId == RG_NUT_UPGRADE_INF && realGI->modIndex == MOD_RANDOMIZER) {
                        SetRandoInf(RAND_INF_HAS_INFINITE_NUT_UPGRADE, true);
                        break;
                    }
                    auto currentLevel = CurrentUpgrade(UPG_NUTS);
                    auto newLevel = currentLevel + (!state ? -1 : 1);
                    if (currentLevel == 0 && state || currentLevel == 1 && !state) {
                        SetInventory(ITEM_NUT, (!state ? ITEM_NONE : ITEM_NUT));
                    }
                    SetUpgrade(UPG_NUTS, newLevel);
                } break;
                case RG_PROGRESSIVE_STICK_UPGRADE: {
                    auto realGI = item.GetGIEntry();
                    if (realGI->itemId == RG_STICK_UPGRADE_INF && realGI->modIndex == MOD_RANDOMIZER) {
                        SetRandoInf(RAND_INF_HAS_INFINITE_STICK_UPGRADE, true);
                        break;
                    }
                    auto currentLevel = CurrentUpgrade(UPG_STICKS);
                    auto newLevel = currentLevel + (!state ? -1 : 1);
                    if (currentLevel == 0 && state || currentLevel == 1 && !state) {
                        SetInventory(ITEM_STICK, (!state ? ITEM_NONE : ITEM_STICK));
                    }
                    SetUpgrade(UPG_STICKS, newLevel);
                } break;
                case RG_PROGRESSIVE_BOMBCHU_BAG: {
                    auto realGI = item.GetGIEntry();
                    if (realGI->itemId == RG_BOMBCHU_INF && realGI->modIndex == MOD_RANDOMIZER) {
                        SetRandoInf(RAND_INF_HAS_INFINITE_BOMBCHUS, true);
                        break;
                    }
                    SetInventory(ITEM_BOMBCHU, (!state ? ITEM_NONE : ITEM_BOMBCHU));
                } break;
                case RG_PROGRESSIVE_MAGIC_METER: {
                    auto realGI = item.GetGIEntry();
                    if (realGI->itemId == RG_MAGIC_INF && realGI->modIndex == MOD_RANDOMIZER) {
                        SetRandoInf(RAND_INF_HAS_INFINITE_MAGIC_METER, true);
                        break;
                    }
                    mSaveContext->magicLevel += (!state ? -1 : 1);
                } break;
                case RG_PROGRESSIVE_OCARINA: {
                    uint8_t i;
                    for (i = 0; i < 3; i++) {
                        if (CurrentInventory(ITEM_OCARINA_FAIRY) == OcarinaLookup[i]) {
                            break;
                        }
                    }
                    i += (!state ? -1 : 1);
                    if (i < 0) {
                        i = 0;
                    } else if (i > 2) {
                        i = 2;
                    }
                    SetInventory(ITEM_OCARINA_FAIRY, OcarinaLookup[i]);
                } break;
                case RG_HEART_CONTAINER:
                    mSaveContext->healthCapacity += (!state ? -16 : 16);
                    break;
                case RG_PIECE_OF_HEART:
                    mSaveContext->healthCapacity += (!state ? -4 : 4);
                    break;
                case RG_BOOMERANG:
                case RG_LENS_OF_TRUTH:
                case RG_MEGATON_HAMMER:
                case RG_DINS_FIRE:
                case RG_FARORES_WIND:
                case RG_NAYRUS_LOVE:
                case RG_FIRE_ARROWS:
                case RG_ICE_ARROWS:
                case RG_LIGHT_ARROWS:
                    SetInventory(item.GetGIEntry()->itemId, (!state ? ITEM_NONE : item.GetGIEntry()->itemId));
                    break;
                case RG_MAGIC_BEAN:
                case RG_MAGIC_BEAN_PACK: {
                    auto change = (item.GetRandomizerGet() == RG_MAGIC_BEAN ? 1 : 10);
                    auto current = GetAmmo(ITEM_BEAN);
                    SetAmmo(ITEM_BEAN, current + (!state ? -change : change));
                } break;
                case RG_EMPTY_BOTTLE:
                case RG_BOTTLE_WITH_MILK:
                case RG_BOTTLE_WITH_RED_POTION:
                case RG_BOTTLE_WITH_GREEN_POTION:
                case RG_BOTTLE_WITH_BLUE_POTION:
                case RG_BOTTLE_WITH_FAIRY:
                case RG_BOTTLE_WITH_FISH:
                case RG_BOTTLE_WITH_BLUE_FIRE:
                case RG_BOTTLE_WITH_BUGS:
                case RG_BOTTLE_WITH_POE:
                case RG_BOTTLE_WITH_BIG_POE: {
                    uint8_t slot = SLOT_BOTTLE_1;
                    while (slot != SLOT_BOTTLE_4) {
                        if (mSaveContext->inventory.items[slot] == ITEM_NONE) {
                            break;
                        }
                        slot++;
                    }
                    uint16_t itemId = item.GetGIEntry()->itemId;
                    if (BottleRandomizerGetToItemID.contains(randoGet)) {
                        itemId = BottleRandomizerGetToItemID[randoGet];
                    }
                    if (randoGet == RG_BOTTLE_WITH_BIG_POE) {
                        BigPoes++;
                    }
                    mSaveContext->inventory.items[slot] = static_cast<uint8_t>(itemId);
                } break;
                case RG_RUTOS_LETTER:
                    SetRandoInf(RAND_INF_OBTAINED_RUTOS_LETTER, state);
                    break;
                case RG_DEATH_MOUNTAIN_CRATER_BEAN_SOUL:
                case RG_DEATH_MOUNTAIN_TRAIL_BEAN_SOUL:
                case RG_DESERT_COLOSSUS_BEAN_SOUL:
                case RG_GERUDO_VALLEY_BEAN_SOUL:
                case RG_GRAVEYARD_BEAN_SOUL:
                case RG_KOKIRI_FOREST_BEAN_SOUL:
                case RG_LAKE_HYLIA_BEAN_SOUL:
                case RG_LOST_WOODS_BRIDGE_BEAN_SOUL:
                case RG_LOST_WOODS_BEAN_SOUL:
                case RG_ZORAS_RIVER_BEAN_SOUL:
                case RG_GOHMA_SOUL:
                case RG_KING_DODONGO_SOUL:
                case RG_BARINADE_SOUL:
                case RG_PHANTOM_GANON_SOUL:
                case RG_VOLVAGIA_SOUL:
                case RG_MORPHA_SOUL:
                case RG_BONGO_BONGO_SOUL:
                case RG_TWINROVA_SOUL:
                case RG_GANON_SOUL:
                case RG_OCARINA_A_BUTTON:
                case RG_OCARINA_C_UP_BUTTON:
                case RG_OCARINA_C_DOWN_BUTTON:
                case RG_OCARINA_C_LEFT_BUTTON:
                case RG_OCARINA_C_RIGHT_BUTTON:
                case RG_KEATON_MASK:
                case RG_SKULL_MASK:
                case RG_SPOOKY_MASK:
                case RG_BUNNY_HOOD:
                case RG_GORON_MASK:
                case RG_ZORA_MASK:
                case RG_GERUDO_MASK:
                case RG_MASK_OF_TRUTH:
                case RG_GREG_RUPEE:
                case RG_SPEAK_DEKU:
                case RG_SPEAK_GERUDO:
                case RG_SPEAK_GORON:
                case RG_SPEAK_HYLIAN:
                case RG_SPEAK_KOKIRI:
                case RG_SPEAK_ZORA:
                case RG_FISHING_POLE:
                case RG_GUARD_HOUSE_KEY:
                case RG_MARKET_BAZAAR_KEY:
                case RG_MARKET_POTION_SHOP_KEY:
                case RG_MASK_SHOP_KEY:
                case RG_MARKET_SHOOTING_GALLERY_KEY:
                case RG_BOMBCHU_BOWLING_KEY:
                case RG_TREASURE_CHEST_GAME_BUILDING_KEY:
                case RG_BOMBCHU_SHOP_KEY:
                case RG_RICHARDS_HOUSE_KEY:
                case RG_ALLEY_HOUSE_KEY:
                case RG_KAK_BAZAAR_KEY:
                case RG_KAK_POTION_SHOP_KEY:
                case RG_BOSS_HOUSE_KEY:
                case RG_GRANNYS_POTION_SHOP_KEY:
                case RG_SKULLTULA_HOUSE_KEY:
                case RG_IMPAS_HOUSE_KEY:
                case RG_WINDMILL_KEY:
                case RG_KAK_SHOOTING_GALLERY_KEY:
                case RG_DAMPES_HUT_KEY:
                case RG_TALONS_HOUSE_KEY:
                case RG_STABLES_KEY:
                case RG_BACK_TOWER_KEY:
                case RG_HYLIA_LAB_KEY:
                case RG_FISHING_HOLE_KEY:
                    SetRandoInf(RandoGetToRandInf.at(randoGet), state);
                    break;
                case RG_TRIFORCE_PIECE:
                    mSaveContext->ship.quest.data.randomizer.triforcePiecesCollected += (!state ? -1 : 1);
                    break;
                case RG_BOMBCHU_5:
                case RG_BOMBCHU_10:
                case RG_BOMBCHU_20:
                    SetInventory(ITEM_BOMBCHU, (!state ? ITEM_NONE : ITEM_BOMBCHU));
                    break;
                default:
                    break;
            }
        } break;
        case ITEMTYPE_EQUIP: {
            RandomizerGet itemRG = item.GetRandomizerGet();
            // Finding a non-shop shield/tunic unlocks its matching shop copy when that gate is enabled.
            switch (itemRG) {
                case RG_DEKU_SHIELD:
                    SetRandoInf(RAND_INF_HAS_FOUND_DEKU_SHIELD, state);
                    break;
                case RG_HYLIAN_SHIELD:
                    SetRandoInf(RAND_INF_HAS_FOUND_HYLIAN_SHIELD, state);
                    break;
                case RG_GORON_TUNIC:
                    SetRandoInf(RAND_INF_HAS_FOUND_GORON_TUNIC, state);
                    break;
                case RG_ZORA_TUNIC:
                    SetRandoInf(RAND_INF_HAS_FOUND_ZORA_TUNIC, state);
                    break;
                default:
                    break;
            }
            if (itemRG == RG_DEKU_SHIELD || itemRG == RG_HYLIAN_SHIELD) {
                return;
            }
            uint32_t equipId = RandoGetToEquipFlag.find(itemRG)->second;
            if (!state) {
                mSaveContext->inventory.equipment &= ~equipId;
                if (equipId == EQUIP_FLAG_SWORD_BGS && itemRG != RG_GIANTS_KNIFE) {
                    mSaveContext->bgsFlag = false;
                }
            } else {
                mSaveContext->inventory.equipment |= equipId;
                if (equipId == EQUIP_FLAG_SWORD_BGS && itemRG != RG_GIANTS_KNIFE) {
                    mSaveContext->bgsFlag = true;
                }
            }
        } break;
        case ITEMTYPE_DUNGEONREWARD:
        case ITEMTYPE_SONG:
            SetQuestItem(RandoGetToQuestItem.find(item.GetRandomizerGet())->second, state);
            break;
        case ITEMTYPE_MAP:
            SetDungeonItem(DUNGEON_MAP, RandoGetToDungeonScene.find(item.GetRandomizerGet())->second, state);
            break;
        case ITEMTYPE_COMPASS:
            SetDungeonItem(DUNGEON_COMPASS, RandoGetToDungeonScene.find(item.GetRandomizerGet())->second, state);
            break;
        case ITEMTYPE_BOSSKEY:
            SetDungeonItem(DUNGEON_KEY_BOSS, RandoGetToDungeonScene.find(item.GetRandomizerGet())->second, state);
            break;
        case ITEMTYPE_FORTRESS_SMALLKEY:
        case ITEMTYPE_SMALLKEY: {
            auto randoGet = item.GetRandomizerGet();
            auto keyring = randoGet >= RG_FOREST_TEMPLE_KEY_RING && randoGet <= RG_GANONS_CASTLE_KEY_RING;
            auto dungeonIndex = RandoGetToDungeonScene.find(randoGet)->second;
            auto count = GetSmallKeyCount(dungeonIndex);
            if (!state) {
                if (keyring) {
                    count = 0;
                } else {
                    count -= 1;
                }
            } else {
                if (keyring) {
                    count = 10;
                } else {
                    count += 1;
                }
            }
            SetSmallKeyCount(dungeonIndex, count);
        } break;
        case ITEMTYPE_TOKEN:
            mSaveContext->inventory.gsTokens += (!state ? -1 : 1);
            break;
        case ITEMTYPE_EVENT:
            break;
        case ITEMTYPE_DROP:
        case ITEMTYPE_REFILL:
        case ITEMTYPE_SHOP: {
            RandomizerGet itemRG = item.GetRandomizerGet();
            if (itemRG == RG_BUY_HYLIAN_SHIELD || itemRG == RG_BUY_DEKU_SHIELD || itemRG == RG_BUY_GORON_TUNIC ||
                itemRG == RG_BUY_ZORA_TUNIC) {
                uint32_t equipId = RandoGetToEquipFlag.find(itemRG)->second;
                if (!state) {
                    mSaveContext->inventory.equipment &= ~equipId;
                } else {
                    mSaveContext->inventory.equipment |= equipId;
                }
            }
            switch (itemRG) {
                case RG_DEKU_NUTS_5:
                case RG_DEKU_NUTS_10:
                case RG_BUY_DEKU_NUTS_5:
                case RG_BUY_DEKU_NUTS_10:
                    SetInventory(ITEM_NUT, (!state ? ITEM_NONE : ITEM_NUT));
                    break;
                case RG_DEKU_STICK_1:
                case RG_BUY_DEKU_STICK_1:
                case RG_STICKS:
                    SetInventory(ITEM_STICK, (!state ? ITEM_NONE : ITEM_STICK));
                    break;
                case RG_BOMBCHU_5:
                case RG_BOMBCHU_10:
                case RG_BOMBCHU_20:
                    SetInventory(ITEM_BOMBCHU, (!state ? ITEM_NONE : ITEM_BOMBCHU));
                    break;
                default:
                    break;
            }
        } break;
    }
}

SaveContext* Logic::GetSaveContext() {
    if (mSaveContext == nullptr) {
        NewSaveContext();
    }
    return mSaveContext;
}

void Logic::SetSaveContext(SaveContext* context) {
    mSaveContext = context;
}

void Logic::InitSaveContext() {
    mSaveContext->totalDays = 0;
    mSaveContext->bgsDayCount = 0;

    mSaveContext->deaths = 0;
    for (int i = 0; i < ARRAY_COUNT(mSaveContext->playerName); i++) {
        mSaveContext->playerName[i] = 0x3E;
    }
    mSaveContext->n64ddFlag = 0;
    mSaveContext->healthCapacity = 0x30;
    mSaveContext->health = 0x30;
    mSaveContext->magicLevel = 0;
    mSaveContext->magic = 0x30;
    mSaveContext->rupees = 0;
    mSaveContext->swordHealth = 0;
    mSaveContext->naviTimer = 0;
    mSaveContext->isMagicAcquired = 0;
    mSaveContext->isDoubleMagicAcquired = 0;
    mSaveContext->isDoubleDefenseAcquired = 0;
    mSaveContext->bgsFlag = 0;
    mSaveContext->ocarinaGameRoundNum = 0;
    for (int button = 0; button < ARRAY_COUNT(mSaveContext->childEquips.buttonItems); button++) {
        mSaveContext->childEquips.buttonItems[button] = ITEM_NONE;
    }
    for (int button = 0; button < ARRAY_COUNT(mSaveContext->childEquips.cButtonSlots); button++) {
        mSaveContext->childEquips.cButtonSlots[button] = SLOT_NONE;
    }
    mSaveContext->childEquips.equipment = 0;
    for (int button = 0; button < ARRAY_COUNT(mSaveContext->adultEquips.buttonItems); button++) {
        mSaveContext->adultEquips.buttonItems[button] = ITEM_NONE;
    }
    for (int button = 0; button < ARRAY_COUNT(mSaveContext->adultEquips.cButtonSlots); button++) {
        mSaveContext->adultEquips.cButtonSlots[button] = SLOT_NONE;
    }
    mSaveContext->adultEquips.equipment = 0;
    mSaveContext->unk_54 = 0;
    mSaveContext->savedSceneNum = SCENE_LINKS_HOUSE;

    // Equipment
    for (int button = 0; button < ARRAY_COUNT(mSaveContext->equips.buttonItems); button++) {
        mSaveContext->equips.buttonItems[button] = ITEM_NONE;
    }
    for (int button = 0; button < ARRAY_COUNT(mSaveContext->equips.cButtonSlots); button++) {
        mSaveContext->equips.cButtonSlots[button] = SLOT_NONE;
    }
    mSaveContext->equips.equipment = 0;

    // Inventory
    for (int item = 0; item < ARRAY_COUNT(mSaveContext->inventory.items); item++) {
        mSaveContext->inventory.items[item] = ITEM_NONE;
    }
    for (int ammo = 0; ammo < ARRAY_COUNT(mSaveContext->inventory.ammo); ammo++) {
        mSaveContext->inventory.ammo[ammo] = 0;
    }
    mSaveContext->inventory.equipment = 0;
    mSaveContext->inventory.upgrades = 0;
    mSaveContext->inventory.questItems = 0;
    for (int dungeon = 0; dungeon < ARRAY_COUNT(mSaveContext->inventory.dungeonItems); dungeon++) {
        mSaveContext->inventory.dungeonItems[dungeon] = 0;
    }
    for (int dungeon = 0; dungeon < ARRAY_COUNT(mSaveContext->inventory.dungeonKeys); dungeon++) {
        mSaveContext->inventory.dungeonKeys[dungeon] = 0x0;
    }
    mSaveContext->inventory.defenseHearts = 0;
    mSaveContext->inventory.gsTokens = 0;
    for (int scene = 0; scene < ARRAY_COUNT(mSaveContext->sceneFlags); scene++) {
        mSaveContext->sceneFlags[scene].chest = 0;
        mSaveContext->sceneFlags[scene].swch = 0;
        mSaveContext->sceneFlags[scene].clear = 0;
        mSaveContext->sceneFlags[scene].collect = 0;
        mSaveContext->sceneFlags[scene].unk = 0;
        mSaveContext->sceneFlags[scene].rooms = 0;
        mSaveContext->sceneFlags[scene].floors = 0;
    }
    mSaveContext->fw.pos.x = 0;
    mSaveContext->fw.pos.y = 0;
    mSaveContext->fw.pos.z = 0;
    mSaveContext->fw.yaw = 0;
    mSaveContext->fw.playerParams = 0;
    mSaveContext->fw.entranceIndex = 0;
    mSaveContext->fw.roomIndex = 0;
    mSaveContext->fw.set = 0;
    mSaveContext->fw.tempSwchFlags = 0;
    mSaveContext->fw.tempCollectFlags = 0;
    for (int flag = 0; flag < ARRAY_COUNT(mSaveContext->gsFlags); flag++) {
        mSaveContext->gsFlags[flag] = 0;
    }
    for (int highscore = 0; highscore < ARRAY_COUNT(mSaveContext->highScores); highscore++) {
        mSaveContext->highScores[highscore] = 0;
    }
    for (int flag = 0; flag < ARRAY_COUNT(mSaveContext->eventChkInf); flag++) {
        mSaveContext->eventChkInf[flag] = 0;
    }
    for (int flag = 0; flag < ARRAY_COUNT(mSaveContext->itemGetInf); flag++) {
        mSaveContext->itemGetInf[flag] = 0;
    }
    for (int flag = 0; flag < ARRAY_COUNT(mSaveContext->infTable); flag++) {
        mSaveContext->infTable[flag] = 0;
    }
    mSaveContext->worldMapAreaData = 0;
    mSaveContext->scarecrowLongSongSet = 0;
    for (int i = 0; i < ARRAY_COUNT(mSaveContext->scarecrowLongSong); i++) {
        mSaveContext->scarecrowLongSong[i].noteIdx = 0;
        mSaveContext->scarecrowLongSong[i].unk_01 = 0;
        mSaveContext->scarecrowLongSong[i].unk_02 = 0;
        mSaveContext->scarecrowLongSong[i].volume = 0;
        mSaveContext->scarecrowLongSong[i].vibrato = 0;
        mSaveContext->scarecrowLongSong[i].tone = 0;
        mSaveContext->scarecrowLongSong[i].semitone = 0;
    }
    mSaveContext->scarecrowSpawnSongSet = 0;
    for (int i = 0; i < ARRAY_COUNT(mSaveContext->scarecrowSpawnSong); i++) {
        mSaveContext->scarecrowSpawnSong[i].noteIdx = 0;
        mSaveContext->scarecrowSpawnSong[i].unk_01 = 0;
        mSaveContext->scarecrowSpawnSong[i].unk_02 = 0;
        mSaveContext->scarecrowSpawnSong[i].volume = 0;
        mSaveContext->scarecrowSpawnSong[i].vibrato = 0;
        mSaveContext->scarecrowSpawnSong[i].tone = 0;
        mSaveContext->scarecrowSpawnSong[i].semitone = 0;
    }

    mSaveContext->horseData.scene = SCENE_HYRULE_FIELD;
    mSaveContext->horseData.pos.x = -1840;
    mSaveContext->horseData.pos.y = 72;
    mSaveContext->horseData.pos.z = 5497;
    mSaveContext->horseData.angle = -0x6AD9;
    mSaveContext->magicLevel = 0;
    mSaveContext->infTable[29] = 1;
    mSaveContext->sceneFlags[5].swch = 0x40000000;

    // SoH specific
    mSaveContext->ship.backupFW = mSaveContext->fw;
    mSaveContext->ship.pendingSale = ITEM_NONE;
    mSaveContext->ship.pendingSaleMod = MOD_NONE;
    mSaveContext->ship.pendingIceTrapCount = 0;

    // Init with normal quest unless only an MQ rom is provided
    mSaveContext->ship.quest.id = OTRGlobals::Instance->HasOriginal() ? QUEST_NORMAL : QUEST_MASTER;

    // RANDOTODO (ADD ITEMLOCATIONS TO GSAVECONTEXT)
}

void Logic::NewSaveContext() {
    // `delete`, matching the `new` two lines down. `free()` on a `new`ed pointer is undefined even
    // for a POD, and AddressSanitizer names it: alloc-dealloc-mismatch (operator new vs free), one
    // per AssumedFill round of a seed generation. The `!= &gSaveContext` guard stays -- the logic can
    // be pointed at the real save, which is a global and belongs to nobody here.
    if (mSaveContext != nullptr && mSaveContext != &gSaveContext) {
        delete mSaveContext;
    }
    mSaveContext = new SaveContext();
    InitSaveContext();
}

uint8_t Logic::InventorySlot(uint32_t item) {
    return gItemSlots[item];
}

uint32_t Logic::CurrentUpgrade(uint32_t upgrade) {
    return (mSaveContext->inventory.upgrades & gUpgradeMasks[upgrade]) >> gUpgradeShifts[upgrade];
}

uint32_t Logic::CurrentInventory(uint32_t item) {
    return mSaveContext->inventory.items[InventorySlot(item)];
}

void Logic::SetUpgrade(uint32_t upgrade, uint8_t level) {
    mSaveContext->inventory.upgrades &= gUpgradeNegMasks[upgrade];
    mSaveContext->inventory.upgrades |= level << gUpgradeShifts[upgrade];
}

bool Logic::CheckInventory(uint32_t item, bool exact) {
    auto current = mSaveContext->inventory.items[InventorySlot(item)];
    return exact ? (current == item) : (current != ITEM_NONE);
}

void Logic::SetInventory(uint32_t itemSlot, uint32_t item) {
    mSaveContext->inventory.items[InventorySlot(itemSlot)] = item;
}

bool Logic::CheckEquipment(uint32_t equipFlag) {
    return (equipFlag & mSaveContext->inventory.equipment);
}

bool Logic::CheckQuestItem(uint32_t item) {
    return ((1 << item) & mSaveContext->inventory.questItems);
}

void Logic::SetQuestItem(uint32_t item, bool state) {
    if (!state) {
        mSaveContext->inventory.questItems &= ~(1 << item);
    } else {
        mSaveContext->inventory.questItems |= (1 << item);
    }
}

// Get the swch bit positions for the dungeon
const std::vector<uint8_t>& GetDungeonSmallKeyDoors(const SceneID sceneId) {
    static const std::vector<uint8_t> emptyVector;

    static const std::vector<uint8_t> normalSmallKeyDoors{ 1, 2, 3, 4 };
    static const std::vector<uint8_t> fastSmallKeyDoors{ 1 };
    static const std::vector<uint8_t> freeSmallKeyDoors{};

    using SmallKeyDoorSets = std::pair<std::vector<uint8_t>, std::vector<uint8_t>>; // first = vanilla, second = MQ
    static const std::unordered_map<SceneID, SmallKeyDoorSets> dungeonSmallKeyDoors{
        { SCENE_FOREST_TEMPLE, { { 0, 1, 2, 3, 4 }, { 0, 1, 2, 3, 4, 6 } } },
        { SCENE_FIRE_TEMPLE, { { 23, 24, 25, 26, 27, 29, 30, 31 }, { 23, 24, 26, 27, 30 } } },
        { SCENE_WATER_TEMPLE, { { 1, 2, 5, 6, 9 }, { 4, 21 } } },
        { SCENE_SPIRIT_TEMPLE, { { 13, 21, 27, 28, 30 }, { 1, 3, 18, 21, 27, 28, 30 } } },
        { SCENE_SHADOW_TEMPLE, { { 21, 22, 23, 24, 25 }, { 21, 22, 23, 24, 25, 27 } } },
        { SCENE_BOTTOM_OF_THE_WELL, { { 27, 28, 29 }, { 20, 21 } } },
        { SCENE_GERUDO_TRAINING_GROUND, { { 1, 3, 4, 5, 6, 7, 9, 10, 23 }, { 20, 23, 29 } } },
        { SCENE_INSIDE_GANONS_CASTLE, { { 29, 30 }, { 20, 21, 22 } } },
    };
    static const std::vector<uint8_t> vanillaWaterTempleDoors{ 1, 2, 5, 6, 9, 21 };

    if (sceneId == SCENE_THIEVES_HIDEOUT) {
        if (RAND_GET_OPTION(RSK_GERUDO_FORTRESS).Is(RO_GF_CARPENTERS_NORMAL)) {
            return normalSmallKeyDoors;
        }
        if (RAND_GET_OPTION(RSK_GERUDO_FORTRESS).Is(RO_GF_CARPENTERS_FAST)) {
            return fastSmallKeyDoors;
        }
        return freeSmallKeyDoors;
    }

    if (sceneId == SCENE_WATER_TEMPLE && IS_VANILLA) {
        return vanillaWaterTempleDoors;
    }

    auto dungeonInfo = Rando::Context::GetInstance()->GetDungeons()->GetDungeonFromScene(sceneId);
    if (dungeonInfo == nullptr) {
        return emptyVector;
    }

    auto it = dungeonSmallKeyDoors.find(sceneId);
    if (it == dungeonSmallKeyDoors.end()) {
        return emptyVector;
    }

    return dungeonInfo->IsMQ() ? it->second.second : it->second.first;
}

int8_t Logic::GetUsedSmallKeyCount(SceneID sceneId) {
    const auto& smallKeyDoors = GetDungeonSmallKeyDoors(sceneId);

    // Get the swch value for the scene
    uint32_t swch;
    if (gPlayState != nullptr && gPlayState->sceneNum == sceneId) {
        swch = gPlayState->actorCtx.flags.swch;
    } else {
        swch = mSaveContext->sceneFlags[sceneId].swch;
    }

    // Count the number of small keys doors unlocked
    int8_t unlockedSmallKeyDoors = 0;
    for (auto& smallKeyDoor : smallKeyDoors) {
        unlockedSmallKeyDoors += swch >> smallKeyDoor & 1;
    }

    // RANDOTODO: Account for MQ Water trick that causes the basement lock to unlock when the player clears the stalfos
    // pit.
    return unlockedSmallKeyDoors;
}

uint8_t Logic::GetSmallKeyCount(uint32_t dungeonIndex) {
    int8_t dungeonKeys = mSaveContext->inventory.dungeonKeys[dungeonIndex];
    if (dungeonKeys == -1) {
        // never got keys, so can't have used keys
        return 0;
    }
    return dungeonKeys + GetUsedSmallKeyCount(SceneID(dungeonIndex));
}

void Logic::SetSmallKeyCount(uint32_t dungeonIndex, uint8_t count) {
    mSaveContext->inventory.dungeonKeys[dungeonIndex] = count;
}

bool Logic::CheckDungeonItem(uint32_t item, uint32_t dungeonIndex) {
    return mSaveContext->inventory.dungeonItems[dungeonIndex] & gBitFlags[item];
}

void Logic::SetDungeonItem(uint32_t item, uint32_t dungeonIndex, bool state) {
    if (!state) {
        mSaveContext->inventory.dungeonItems[dungeonIndex] &= ~gBitFlags[item];
    } else {
        mSaveContext->inventory.dungeonItems[dungeonIndex] |= gBitFlags[item];
    }
}

bool Logic::CheckRandoInf(uint32_t flag) {
    return mSaveContext->ship.randomizerInf[flag >> 4] & (1 << (flag & 0xF));
}

void Logic::SetRandoInf(uint32_t flag, bool state) {
    if (!state) {
        mSaveContext->ship.randomizerInf[flag >> 4] &= ~(1 << (flag & 0xF));
    } else {
        mSaveContext->ship.randomizerInf[flag >> 4] |= (1 << (flag & 0xF));
    }
}

bool Logic::CheckEventChkInf(int32_t flag) {
    return mSaveContext->eventChkInf[flag >> 4] & (1 << (flag & 0xF));
}

void Logic::SetEventChkInf(int32_t flag, bool state) {
    if (!state) {
        mSaveContext->eventChkInf[flag >> 4] &= ~(1 << (flag & 0xF));
    } else {
        mSaveContext->eventChkInf[flag >> 4] |= (1 << (flag & 0xF));
    }
}

uint8_t Logic::GetGSCount() {
    return static_cast<uint8_t>(mSaveContext->inventory.gsTokens);
}

uint8_t Logic::GetAmmo(uint32_t item) {
    return mSaveContext->inventory.ammo[gItemSlots[item]];
}

void Logic::SetAmmo(uint32_t item, uint8_t count) {
    mSaveContext->inventory.ammo[gItemSlots[item]] = count;
}

} // namespace Rando
