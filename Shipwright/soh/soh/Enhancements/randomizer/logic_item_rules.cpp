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

bool Logic::HasItem(RandomizerGet itemName) {
    switch (itemName) {
        case RG_FAIRY_OCARINA:
            return CheckInventory(ITEM_OCARINA_FAIRY, false);
        case RG_OCARINA_OF_TIME:
            return CheckInventory(ITEM_OCARINA_TIME, true);
        case RG_DINS_FIRE:
            return CheckInventory(ITEM_DINS_FIRE, true);
        case RG_FARORES_WIND:
            return CheckInventory(ITEM_FARORES_WIND, true);
        case RG_NAYRUS_LOVE:
            return CheckInventory(ITEM_NAYRUS_LOVE, true);
        case RG_LENS_OF_TRUTH:
            return CheckInventory(ITEM_LENS, true);
        case RG_FAIRY_BOW:
            return CheckInventory(ITEM_BOW, true);
        case RG_MEGATON_HAMMER:
            return CheckInventory(ITEM_HAMMER, true);
        case RG_HOOKSHOT:
            return CheckInventory(ITEM_HOOKSHOT, false);
        case RG_LONGSHOT:
            return CheckInventory(ITEM_LONGSHOT, true);
        case RG_PROGRESSIVE_STICK_UPGRADE:
        case RG_STICKS:
            return CurrentUpgrade(UPG_STICKS);
        case RG_FIRE_ARROWS:
            return CheckInventory(ITEM_ARROW_FIRE, true);
        case RG_ICE_ARROWS:
            return CheckInventory(ITEM_ARROW_ICE, true);
        case RG_LIGHT_ARROWS:
            return CheckInventory(ITEM_ARROW_LIGHT, true);
        case RG_PROGRESSIVE_BOMBCHU_BAG:
        case RG_BOMBCHU_5:
        case RG_BOMBCHU_10:
        case RG_BOMBCHU_20:
            return (BombchusEnabled() &&
                    (Get(LOGIC_BUY_BOMBCHUS) || Get(LOGIC_COULD_PLAY_BOWLING) || Get(LOGIC_CARPET_MERCHANT))) ||
                   CheckInventory(ITEM_BOMBCHU, true);
        case RG_FAIRY_SLINGSHOT:
            return CheckInventory(ITEM_SLINGSHOT, true);
        case RG_BOOMERANG:
            return CheckInventory(ITEM_BOOMERANG, true);
        case RG_PROGRESSIVE_NUT_UPGRADE:
        case RG_NUTS:
            return CurrentUpgrade(UPG_NUTS);
        case RG_MAGIC_BEAN:
            return GetAmmo(ITEM_BEAN) > 0 || CheckInventory(ITEM_BEAN, true);
        case RG_KOKIRI_SWORD:
        case RG_DEKU_SHIELD:
        case RG_GORON_TUNIC:
        case RG_ZORA_TUNIC:
        case RG_HYLIAN_SHIELD:
        case RG_MIRROR_SHIELD:
        case RG_MASTER_SWORD:
        case RG_IRON_BOOTS:
        case RG_HOVER_BOOTS:
            return CheckEquipment(RandoGetToEquipFlag.at(itemName));
        case RG_GIANTS_KNIFE:
            return CheckEquipment(RandoGetToEquipFlag.at(itemName)) || Get(LOGIC_MEDIGORON);
        case RG_BIGGORON_SWORD:
            return CheckEquipment(RandoGetToEquipFlag.at(itemName)) && mSaveContext->bgsFlag;
        case RG_POWER_BRACELET:
            return CheckRandoInf(RAND_INF_CAN_GRAB);
        case RG_GORONS_BRACELET:
            return CurrentUpgrade(UPG_STRENGTH);
        case RG_SILVER_GAUNTLETS:
            return CurrentUpgrade(UPG_STRENGTH) >= 2;
        case RG_GOLDEN_GAUNTLETS:
            return CurrentUpgrade(UPG_STRENGTH) >= 3;
        case RG_PROGRESSIVE_BOMB_BAG:
        case RG_BOMB_BAG:
            return CurrentUpgrade(UPG_BOMB_BAG);
        case RG_MAGIC_SINGLE:
            return GetSaveContext()->magicLevel >= 1 || GetSaveContext()->isMagicAcquired;
            // Songs
        case RG_ZELDAS_LULLABY:
        case RG_EPONAS_SONG:
        case RG_SARIAS_SONG:
        case RG_SUNS_SONG:
        case RG_SONG_OF_TIME:
        case RG_SONG_OF_STORMS:
        case RG_MINUET_OF_FOREST:
        case RG_BOLERO_OF_FIRE:
        case RG_SERENADE_OF_WATER:
        case RG_REQUIEM_OF_SPIRIT:
        case RG_NOCTURNE_OF_SHADOW:
        case RG_PRELUDE_OF_LIGHT:
            // Dungeon Rewards
        case RG_KOKIRI_EMERALD:
        case RG_GORON_RUBY:
        case RG_ZORA_SAPPHIRE:
        case RG_FOREST_MEDALLION:
        case RG_FIRE_MEDALLION:
        case RG_WATER_MEDALLION:
        case RG_SPIRIT_MEDALLION:
        case RG_SHADOW_MEDALLION:
        case RG_LIGHT_MEDALLION:
            // Misc Quest Items
        case RG_STONE_OF_AGONY:
        case RG_GERUDO_MEMBERSHIP_CARD:
            return CheckQuestItem(RandoGetToQuestItem.at(itemName));
        case RG_DOUBLE_DEFENSE:
            return GetSaveContext()->isDoubleDefenseAcquired;
            // Masks
        case RG_SKULL_MASK:
            switch (ctx->GetOption(RSK_MASK_QUEST).Get()) {
                case RO_MASK_QUEST_VANILLA:
                    return Get(LOGIC_BORROW_SKULL_MASK);
                case RO_MASK_QUEST_COMPLETED:
                    return HasItem(RG_ZELDAS_LETTER) && Get(LOGIC_KAKARIKO_GATE_OPEN);
                case RO_MASK_QUEST_SHUFFLE:
                    return CheckRandoInf(RAND_INF_CHILD_TRADES_HAS_MASK_SKULL);
                default:
                    assert(false);
                    return false;
            }
        case RG_MASK_OF_TRUTH:
            switch (ctx->GetOption(RSK_MASK_QUEST).Get()) {
                case RO_MASK_QUEST_VANILLA:
                    return Get(LOGIC_BORROW_RIGHT_MASKS);
                case RO_MASK_QUEST_COMPLETED:
                    return HasItem(RG_ZELDAS_LETTER) && Get(LOGIC_KAKARIKO_GATE_OPEN);
                case RO_MASK_QUEST_SHUFFLE:
                    return CheckRandoInf(RAND_INF_CHILD_TRADES_HAS_MASK_TRUTH);
                default:
                    assert(false);
                    return false;
            }
        case RG_FISHING_POLE:
        case RG_ZELDAS_LETTER:
        case RG_WEIRD_EGG:
        case RG_GREG_RUPEE:
        case RG_SPEAK_DEKU:
        case RG_SPEAK_GERUDO:
        case RG_SPEAK_GORON:
        case RG_SPEAK_HYLIAN:
        case RG_SPEAK_KOKIRI:
        case RG_SPEAK_ZORA:
            // Ocarina Buttons
        case RG_OCARINA_A_BUTTON:
        case RG_OCARINA_C_LEFT_BUTTON:
        case RG_OCARINA_C_RIGHT_BUTTON:
        case RG_OCARINA_C_DOWN_BUTTON:
        case RG_OCARINA_C_UP_BUTTON:
            // Bean Souls
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
            // Boss Souls
        case RG_GOHMA_SOUL:
        case RG_KING_DODONGO_SOUL:
        case RG_BARINADE_SOUL:
        case RG_PHANTOM_GANON_SOUL:
        case RG_VOLVAGIA_SOUL:
        case RG_MORPHA_SOUL:
        case RG_BONGO_BONGO_SOUL:
        case RG_TWINROVA_SOUL:
        case RG_GANON_SOUL:
        case RG_SKELETON_KEY:
            // Overworld Keys
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
        case RG_RUTOS_LETTER:
            return CheckRandoInf(RandoGetToRandInf.at(itemName));
            // Boss Keys
        case RG_FOREST_TEMPLE_BOSS_KEY:
        case RG_FIRE_TEMPLE_BOSS_KEY:
        case RG_WATER_TEMPLE_BOSS_KEY:
        case RG_SPIRIT_TEMPLE_BOSS_KEY:
        case RG_SHADOW_TEMPLE_BOSS_KEY:
        case RG_GANONS_CASTLE_BOSS_KEY:
            return CheckDungeonItem(DUNGEON_KEY_BOSS, RandoGetToDungeonScene.at(itemName));
            // Maps
        case RG_DEKU_TREE_MAP:
        case RG_DODONGOS_CAVERN_MAP:
        case RG_JABU_JABUS_BELLY_MAP:
        case RG_FOREST_TEMPLE_MAP:
        case RG_FIRE_TEMPLE_MAP:
        case RG_WATER_TEMPLE_MAP:
        case RG_SPIRIT_TEMPLE_MAP:
        case RG_SHADOW_TEMPLE_MAP:
        case RG_BOTTOM_OF_THE_WELL_MAP:
        case RG_ICE_CAVERN_MAP:
            return CheckDungeonItem(DUNGEON_MAP, RandoGetToDungeonScene.at(itemName));
            // Compasses
        case RG_DEKU_TREE_COMPASS:
        case RG_DODONGOS_CAVERN_COMPASS:
        case RG_JABU_JABUS_BELLY_COMPASS:
        case RG_FOREST_TEMPLE_COMPASS:
        case RG_FIRE_TEMPLE_COMPASS:
        case RG_WATER_TEMPLE_COMPASS:
        case RG_SPIRIT_TEMPLE_COMPASS:
        case RG_SHADOW_TEMPLE_COMPASS:
        case RG_BOTTOM_OF_THE_WELL_COMPASS:
        case RG_ICE_CAVERN_COMPASS:
            return CheckDungeonItem(DUNGEON_COMPASS, RandoGetToDungeonScene.at(itemName));
            // Wallets
        case RG_CHILD_WALLET:
            return CheckRandoInf(RAND_INF_HAS_WALLET);
        case RG_ADULT_WALLET:
            return CurrentUpgrade(UPG_WALLET) >= 1;
        case RG_GIANT_WALLET:
            return CurrentUpgrade(UPG_WALLET) >= 2;
        case RG_TYCOON_WALLET:
            return CurrentUpgrade(UPG_WALLET) >= 3;
            // Scales
        case RG_BRONZE_SCALE:
            return CheckRandoInf(RAND_INF_CAN_SWIM);
        case RG_SILVER_SCALE:
            return CurrentUpgrade(UPG_SCALE) >= 1;
        case RG_GOLDEN_SCALE:
            return CurrentUpgrade(UPG_SCALE) >= 2;
        case RG_CLIMB:
            return CheckRandoInf(RAND_INF_CAN_CLIMB);
        case RG_CRAWL:
            return CheckRandoInf(RAND_INF_CAN_CRAWL);
        case RG_OPEN_CHEST:
            return CheckRandoInf(RAND_INF_CAN_OPEN_CHEST);
        case RG_POCKET_EGG:
            return CheckRandoInf(RAND_INF_ADULT_TRADES_HAS_POCKET_EGG) ||
                   CheckRandoInf(RAND_INF_ADULT_TRADES_HAS_POCKET_CUCCO);
        case RG_COJIRO:
        case RG_ODD_MUSHROOM:
        case RG_ODD_POTION:
        case RG_POACHERS_SAW:
        case RG_BROKEN_SWORD:
        case RG_PRESCRIPTION:
        case RG_EYEBALL_FROG:
        case RG_EYEDROPS:
        case RG_CLAIM_CHECK:
            return CheckRandoInf(itemName - RG_COJIRO + RAND_INF_ADULT_TRADES_HAS_COJIRO);
        case RG_BOTTLE_WITH_BIG_POE:
        case RG_BOTTLE_WITH_BLUE_FIRE:
        case RG_BOTTLE_WITH_BLUE_POTION:
        case RG_BOTTLE_WITH_BUGS:
        case RG_BOTTLE_WITH_FAIRY:
        case RG_BOTTLE_WITH_FISH:
        case RG_BOTTLE_WITH_GREEN_POTION:
        case RG_BOTTLE_WITH_MILK:
        case RG_BOTTLE_WITH_POE:
        case RG_BOTTLE_WITH_RED_POTION:
        case RG_EMPTY_BOTTLE:
            return HasBottle();
        default:
            break;
    }
    SPDLOG_ERROR("HasItem reached `return false;`. Missing case for RandomizerGet of {}",
                 static_cast<uint32_t>(itemName));
    assert(false);
    return false;
}

/* based on sRestrictionFlags in z_parameter.c */
bool Logic::ItemUseAllowed(RandomizerGet itemName) {
    switch (itemName) {
        case RG_KOKIRI_SWORD:
        case RG_MASTER_SWORD:
        case RG_GIANTS_KNIFE:
        case RG_BIGGORON_SWORD:
            return BAllowed();
        case RG_DEKU_SHIELD:
        case RG_HYLIAN_SHIELD:
        case RG_MIRROR_SHIELD:
        case RG_GORON_TUNIC:
        case RG_ZORA_TUNIC:
        case RG_IRON_BOOTS:
        case RG_HOVER_BOOTS:
        case RG_MAGIC_SINGLE:
        case RG_SILVER_GAUNTLETS:
        case RG_GOLDEN_GAUNTLETS:
        case RG_ZELDAS_LULLABY:
        case RG_EPONAS_SONG:
        case RG_PRELUDE_OF_LIGHT:
        case RG_SARIAS_SONG:
        case RG_SONG_OF_TIME:
        case RG_BOLERO_OF_FIRE:
        case RG_REQUIEM_OF_SPIRIT:
        case RG_SONG_OF_STORMS:
        case RG_MINUET_OF_FOREST:
        case RG_SERENADE_OF_WATER:
        case RG_NOCTURNE_OF_SHADOW:
        case RG_CRAWL:
            return true;
        default:
            break;
    }

    // hacky fix for underwater sections TODO this properly with a flag in regions
    if (CurrentRegionKey == RR_LH_LAB_UNDERWATER) {
        return itemName == RG_HOOKSHOT || itemName == RG_LONGSHOT;
    }

    switch (RegionTable(CurrentRegionKey)->scene) {
        case SCENE_DEKU_TREE:
        case SCENE_DODONGOS_CAVERN:
        case SCENE_JABU_JABU:
        case SCENE_FOREST_TEMPLE:
        case SCENE_FIRE_TEMPLE:
        case SCENE_WATER_TEMPLE:
        case SCENE_SPIRIT_TEMPLE:
        case SCENE_SHADOW_TEMPLE:
        case SCENE_BOTTOM_OF_THE_WELL:
        case SCENE_ICE_CAVERN:
        case SCENE_ID_MAX:
            return true;
        case SCENE_HYRULE_FIELD:
        case SCENE_GANONS_TOWER:
        case SCENE_GERUDO_TRAINING_GROUND:
        case SCENE_THIEVES_HIDEOUT:
        case SCENE_INSIDE_GANONS_CASTLE:
        case SCENE_GREAT_FAIRYS_FOUNTAIN_MAGIC:
        case SCENE_FAIRYS_FOUNTAIN:
        case SCENE_GREAT_FAIRYS_FOUNTAIN_SPELLS:
        case SCENE_GROTTOS:
        case SCENE_GRAVE_WITH_FAIRYS_FOUNTAIN:
        case SCENE_REDEAD_GRAVE:
        case SCENE_ROYAL_FAMILYS_TOMB:
        case SCENE_KAKARIKO_VILLAGE:
        case SCENE_GRAVEYARD:
        case SCENE_ZORAS_RIVER:
        case SCENE_KOKIRI_FOREST:
        case SCENE_SACRED_FOREST_MEADOW:
        case SCENE_LAKE_HYLIA:
        case SCENE_ZORAS_DOMAIN:
        case SCENE_ZORAS_FOUNTAIN:
        case SCENE_GERUDO_VALLEY:
        case SCENE_LOST_WOODS:
        case SCENE_DESERT_COLOSSUS:
        case SCENE_GERUDOS_FORTRESS:
        case SCENE_HAUNTED_WASTELAND:
        case SCENE_HYRULE_CASTLE:
        case SCENE_DEATH_MOUNTAIN_TRAIL:
        case SCENE_DEATH_MOUNTAIN_CRATER:
        case SCENE_GORON_CITY:
        case SCENE_LON_LON_RANCH:
        case SCENE_OUTSIDE_GANONS_CASTLE:
            return !(itemName == RG_FARORES_WIND);
        case SCENE_GANONS_TOWER_COLLAPSE_INTERIOR:
        case SCENE_INSIDE_GANONS_CASTLE_COLLAPSE:
        case SCENE_GANONS_TOWER_COLLAPSE_EXTERIOR:
            return !(itemName == RG_FARORES_WIND || itemName == RG_FAIRY_OCARINA || itemName == RG_OCARINA_OF_TIME);
        case SCENE_CASTLE_COURTYARD_ZELDA:
            return !(StaticData::restrictSpells.contains(itemName) || itemName == RG_FAIRY_OCARINA ||
                     itemName == RG_OCARINA_OF_TIME);
        case SCENE_DEKU_TREE_BOSS:
        case SCENE_DODONGOS_CAVERN_BOSS:
        case SCENE_JABU_JABU_BOSS:
        case SCENE_FOREST_TEMPLE_BOSS:
        case SCENE_FIRE_TEMPLE_BOSS:
        case SCENE_WATER_TEMPLE_BOSS:
        case SCENE_SPIRIT_TEMPLE_BOSS:
        case SCENE_SHADOW_TEMPLE_BOSS:
        case SCENE_GANONDORF_BOSS:
        case SCENE_GANON_BOSS:
            return !(StaticData::restrictTrade.contains(itemName) || itemName == RG_FARORES_WIND ||
                     itemName == RG_FAIRY_OCARINA || itemName == RG_OCARINA_OF_TIME);
        case SCENE_WINDMILL_AND_DAMPES_GRAVE:
            return !(StaticData::restrictSpells.contains(itemName));
        case SCENE_MARKET_GUARD_HOUSE:
            return !(StaticData::restrictSpells.contains(itemName) || itemName == RG_HOOKSHOT ||
                     itemName == RG_LONGSHOT);
        case SCENE_MARKET_ENTRANCE_DAY: // test
        case SCENE_MARKET_ENTRANCE_NIGHT:
        case SCENE_MARKET_ENTRANCE_RUINS:
        case SCENE_BACK_ALLEY_DAY:
        case SCENE_BACK_ALLEY_NIGHT:
        case SCENE_MARKET_DAY:
        case SCENE_MARKET_NIGHT:
        case SCENE_MARKET_RUINS:
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_DAY:
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_NIGHT:
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_RUINS:
        case SCENE_KNOW_IT_ALL_BROS_HOUSE:
        case SCENE_TWINS_HOUSE:
        case SCENE_MIDOS_HOUSE:
        case SCENE_SARIAS_HOUSE:
        case SCENE_KAKARIKO_CENTER_GUEST_HOUSE:
        case SCENE_BACK_ALLEY_HOUSE:
        case SCENE_BAZAAR:
        case SCENE_KOKIRI_SHOP:
        case SCENE_GORON_SHOP:
        case SCENE_ZORA_SHOP:
        case SCENE_POTION_SHOP_KAKARIKO:
        case SCENE_BOMBCHU_SHOP:
        case SCENE_HAPPY_MASK_SHOP:
        case SCENE_LINKS_HOUSE:
        case SCENE_DOG_LADY_HOUSE:
        case SCENE_STABLE:
        case SCENE_IMPAS_HOUSE:
        case SCENE_LAKESIDE_LABORATORY:
        case SCENE_CARPENTERS_TENT:
        case SCENE_GRAVEKEEPERS_HUT:
        case SCENE_TEMPLE_OF_TIME:
        case SCENE_LON_LON_BUILDINGS:
        case SCENE_HOUSE_OF_SKULLTULA:
            return StaticData::allowBottleMaskTrade.contains(itemName) || itemName == RG_FAIRY_OCARINA ||
                   itemName == RG_OCARINA_OF_TIME;
        case SCENE_TREASURE_BOX_SHOP:
            return StaticData::allowBottleMaskTrade.contains(itemName) || itemName == RG_LENS_OF_TRUTH;
        case SCENE_POTION_SHOP_GRANNY:
            return StaticData::allowBottleMaskTrade.contains(itemName);
        case SCENE_SHOOTING_GALLERY:
        case SCENE_CASTLE_COURTYARD_GUARDS_DAY:
        case SCENE_CASTLE_COURTYARD_GUARDS_NIGHT:
        case SCENE_BOMBCHU_BOWLING_ALLEY:
            return StaticData::allowMasks.contains(itemName);
        case SCENE_FISHING_POND:
            return itemName == RG_FISHING_POLE;
        default:
            SPDLOG_INFO("ItemUseAllowed reached `default` with item {} in Scene {}.", static_cast<uint32_t>(itemName),
                        static_cast<uint32_t>(RegionTable(CurrentRegionKey)->scene));
            return true;
    }
}

bool Logic::BAllowed() {
    // hacky fix for underwater sections TODO this properly with a flag in regions
    if (CurrentRegionKey == RR_LH_LAB_UNDERWATER) {
        return false;
    }

    switch (RegionTable(CurrentRegionKey)->scene) {
        case SCENE_TREASURE_BOX_SHOP:
        case SCENE_KNOW_IT_ALL_BROS_HOUSE:
        case SCENE_TWINS_HOUSE:
        case SCENE_MIDOS_HOUSE:
        case SCENE_SARIAS_HOUSE:
        case SCENE_KAKARIKO_CENTER_GUEST_HOUSE:
        case SCENE_BACK_ALLEY_HOUSE:
        case SCENE_BAZAAR:
        case SCENE_KOKIRI_SHOP:
        case SCENE_GORON_SHOP:
        case SCENE_ZORA_SHOP:
        case SCENE_POTION_SHOP_KAKARIKO:
        case SCENE_BOMBCHU_SHOP:
        case SCENE_HAPPY_MASK_SHOP:
        case SCENE_LINKS_HOUSE:
        case SCENE_DOG_LADY_HOUSE:
        case SCENE_STABLE:
        case SCENE_IMPAS_HOUSE:
        case SCENE_LAKESIDE_LABORATORY:
        case SCENE_CARPENTERS_TENT:
        case SCENE_GRAVEKEEPERS_HUT:
        case SCENE_SHOOTING_GALLERY:
        case SCENE_BOMBCHU_BOWLING_ALLEY:
        case SCENE_POTION_SHOP_GRANNY:
        case SCENE_CASTLE_COURTYARD_GUARDS_DAY:
        case SCENE_CASTLE_COURTYARD_GUARDS_NIGHT:
        case SCENE_FISHING_POND:
            return false;
        default:
            return true;
    }
}

// Can the passed in item be used?
// RANDOTODO catch magic items explicitly and add an assert on miss.
bool Logic::CanUse(RandomizerGet itemName) {
    if (!HasItem(itemName))
        return false;

    if (!ItemUseAllowed(itemName)) {
        return false;
    }

    switch (itemName) {
        // Magic items
        case RG_MAGIC_SINGLE:
            return true; // AmmoCanDrop || (HasBottle() && Get(LOGIC_BUY_MAGIC_POTION))
        case RG_DINS_FIRE:
        case RG_FARORES_WIND:
        case RG_NAYRUS_LOVE:
        case RG_LENS_OF_TRUTH:
            return CanUse(RG_MAGIC_SINGLE);
        case RG_FIRE_ARROWS:
        case RG_ICE_ARROWS:
        case RG_LIGHT_ARROWS:
            return CanUse(RG_MAGIC_SINGLE) && CanUse(RG_FAIRY_BOW);

        // Adult items
        case RG_FAIRY_BOW:
        case RG_MEGATON_HAMMER:
        case RG_IRON_BOOTS:
        case RG_HOVER_BOOTS:
        case RG_HOOKSHOT:
        case RG_LONGSHOT:
        case RG_GORON_TUNIC:
        case RG_ZORA_TUNIC:
        case RG_MIRROR_SHIELD:
        case RG_MASTER_SWORD:
        case RG_GIANTS_KNIFE:
        case RG_BIGGORON_SWORD:
        case RG_SILVER_GAUNTLETS:
        case RG_GOLDEN_GAUNTLETS:
        // Adult Trade
        case RG_POCKET_EGG:
        case RG_COJIRO:
        case RG_ODD_MUSHROOM:
        case RG_ODD_POTION:
        case RG_POACHERS_SAW:
        case RG_BROKEN_SWORD:
        case RG_PRESCRIPTION:
        case RG_EYEBALL_FROG:
        case RG_EYEDROPS:
        case RG_CLAIM_CHECK:
            return IsAdult;

        // Child items
        case RG_FAIRY_SLINGSHOT:
        case RG_BOOMERANG:
        case RG_KOKIRI_SWORD:
            return IsChild;
        case RG_NUTS:
            return Get(LOGIC_NUT_ACCESS);
        case RG_STICKS:
            return IsChild /* || StickAsAdult;*/ && Get(LOGIC_STICK_ACCESS);
        case RG_DEKU_SHIELD:
            return IsChild; // || DekuShieldAsAdult;
        case RG_PROGRESSIVE_BOMB_BAG:
        case RG_BOMB_BAG:
            return true; // AmmoCanDrop || Get(LOGIC_BUY_BOMB)
        case RG_PROGRESSIVE_BOMBCHU_BAG:
        case RG_BOMBCHU_5:
        case RG_BOMBCHU_10:
        case RG_BOMBCHU_20:
            return BombchuRefill() && BombchusEnabled();
        case RG_WEIRD_EGG:
        case RG_RUTOS_LETTER:
        case RG_MAGIC_BEAN:
        case RG_SKULL_MASK:
        case RG_MASK_OF_TRUTH:
            return IsChild;

        // Songs
        case RG_ZELDAS_LULLABY:
        case RG_EPONAS_SONG:
        case RG_PRELUDE_OF_LIGHT:
            return CanUse(RG_FAIRY_OCARINA) && HasItem(RG_OCARINA_C_LEFT_BUTTON) &&
                   HasItem(RG_OCARINA_C_RIGHT_BUTTON) && HasItem(RG_OCARINA_C_UP_BUTTON);
        case RG_SARIAS_SONG:
            return CanUse(RG_FAIRY_OCARINA) && HasItem(RG_OCARINA_C_LEFT_BUTTON) &&
                   HasItem(RG_OCARINA_C_RIGHT_BUTTON) && HasItem(RG_OCARINA_C_DOWN_BUTTON);
        case RG_SUNS_SONG:
            return CanUse(RG_FAIRY_OCARINA) && HasItem(RG_OCARINA_C_RIGHT_BUTTON) && HasItem(RG_OCARINA_C_UP_BUTTON) &&
                   HasItem(RG_OCARINA_C_DOWN_BUTTON);
        case RG_SONG_OF_TIME:
        case RG_BOLERO_OF_FIRE:
        case RG_REQUIEM_OF_SPIRIT:
            return CanUse(RG_FAIRY_OCARINA) && HasItem(RG_OCARINA_A_BUTTON) && HasItem(RG_OCARINA_C_RIGHT_BUTTON) &&
                   HasItem(RG_OCARINA_C_DOWN_BUTTON);
        case RG_SONG_OF_STORMS:
            return HasItem(RG_FAIRY_OCARINA) && HasItem(RG_OCARINA_A_BUTTON) && HasItem(RG_OCARINA_C_UP_BUTTON) &&
                   HasItem(RG_OCARINA_C_DOWN_BUTTON);
        case RG_MINUET_OF_FOREST:
            return CanUse(RG_FAIRY_OCARINA) && HasItem(RG_OCARINA_A_BUTTON) && HasItem(RG_OCARINA_C_LEFT_BUTTON) &&
                   HasItem(RG_OCARINA_C_RIGHT_BUTTON) && HasItem(RG_OCARINA_C_UP_BUTTON);
        case RG_SERENADE_OF_WATER:
        case RG_NOCTURNE_OF_SHADOW:
            return CanUse(RG_FAIRY_OCARINA) && HasItem(RG_OCARINA_A_BUTTON) && HasItem(RG_OCARINA_C_LEFT_BUTTON) &&
                   HasItem(RG_OCARINA_C_RIGHT_BUTTON) && HasItem(RG_OCARINA_C_DOWN_BUTTON);

        // Misc. Items
        case RG_FISHING_POLE:
            return HasItem(RG_CHILD_WALLET); // as long as you have enough rubies
        case RG_CRAWL:
            return IsChild;

        // Bottle Items
        case RG_BOTTLE_WITH_BUGS:
            return Get(LOGIC_BUG_ACCESS);
        case RG_BOTTLE_WITH_FISH:
            return Get(LOGIC_FISH_ACCESS);
        case RG_BOTTLE_WITH_BLUE_FIRE:
            return Get(LOGIC_BLUE_FIRE_ACCESS);
        case RG_BOTTLE_WITH_FAIRY:
            return Get(LOGIC_FAIRY_ACCESS);

        case RG_FAIRY_OCARINA:
        case RG_OCARINA_OF_TIME:
            return true;

        default:
            SPDLOG_INFO("CanUse reached `default` for {}. using HasItem is a minor Optimisation.",
                        static_cast<uint32_t>(itemName));
            return true;
    }
}

bool Logic::HasProjectile(HasProjectileAge age) {
    return HasExplosives() ||
           (age == HasProjectileAge::Child && (CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_BOOMERANG))) ||
           (age == HasProjectileAge::Adult && (CanUse(RG_HOOKSHOT) || CanUse(RG_FAIRY_BOW))) ||
           (age == HasProjectileAge::Both && (CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_BOOMERANG)) &&
            (CanUse(RG_HOOKSHOT) || CanUse(RG_FAIRY_BOW))) ||
           (age == HasProjectileAge::Either &&
            (CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_BOOMERANG) || CanUse(RG_HOOKSHOT) || CanUse(RG_FAIRY_BOW)));
}

bool Logic::HasBossSoul(RandomizerGet itemName) {
    if (!ctx->GetOption(RSK_SHUFFLE_BOSS_SOULS)) {
        return true;
    }
    switch (itemName) {
        case RG_GOHMA_SOUL:
        case RG_KING_DODONGO_SOUL:
        case RG_BARINADE_SOUL:
        case RG_PHANTOM_GANON_SOUL:
        case RG_VOLVAGIA_SOUL:
        case RG_MORPHA_SOUL:
        case RG_BONGO_BONGO_SOUL:
        case RG_TWINROVA_SOUL:
            return HasItem(itemName);
        case RG_GANON_SOUL:
            return ctx->GetOption(RSK_SHUFFLE_BOSS_SOULS).Is(RO_BOSS_SOULS_ON_PLUS_GANON) ? HasItem(RG_GANON_SOUL)
                                                                                          : true;
        default:
            return false;
    }
}

// RANDOMISERTODO intergrate into HasItem
bool Logic::CanOpenOverworldDoor(RandomizerGet key) {
    if (!ctx->GetOption(RSK_LOCK_OVERWORLD_DOORS)) {
        return true;
    }

    if (HasItem(RG_SKELETON_KEY)) {
        return true;
    }

    return HasItem(key);
}

bool Logic::CanGroundJump(bool hasBombflower) {
    return ctx->GetTrickOption(RT_GROUND_JUMP) && CanStandingShield() &&
           (CanUse(RG_BOMB_BAG) || (hasBombflower && HasItem(RG_GORONS_BRACELET)));
}

bool Logic::CanGroundJumpslash(bool hasBombflower) {
    return ctx->GetTrickOption(RT_GROUND_JUMP_HARD) && CanStandingShield() && CanJumpslash() &&
           (CanUse(RG_BOMB_BAG) || (hasBombflower && HasItem(RG_GORONS_BRACELET)));
}

bool Logic::CanMiddairGroundJump(bool hasBombflower) {
    return ctx->GetTrickOption(RT_GROUND_JUMP_HARD) && CanStandingShield() && CanUse(RG_HOVER_BOOTS) &&
           (CanUse(RG_BOMB_BAG) || (hasBombflower && HasItem(RG_GORONS_BRACELET)));
}

bool Logic::CanOpenUnderwaterChest() {
    return ctx->GetTrickOption(RT_OPEN_UNDERWATER_CHEST) && CanUse(RG_IRON_BOOTS) && CanUse(RG_HOOKSHOT) &&
           HasItem(RG_OPEN_CHEST);
}

} // namespace Rando
