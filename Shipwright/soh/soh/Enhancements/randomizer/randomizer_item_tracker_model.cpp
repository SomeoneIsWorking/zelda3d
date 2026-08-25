#include "randomizer_item_tracker_model.h"

#include <algorithm>

#include "randomizerTypes.h"
#include "randomizer_item_tracker_widgets.h"
#include "soh/OTRGlobals.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/randomizer.h"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "macros.h"
#include "functions/game_state.h"
}

bool shouldUpdateVectors = true;
std::vector<ItemTrackerItem> mainWindowItems = {};

std::vector<ItemTrackerItem> inventoryItems = {
    ITEM_TRACKER_ITEM(ITEM_STICK, 0, DrawItem),       ITEM_TRACKER_ITEM(ITEM_NUT, 0, DrawItem),
    ITEM_TRACKER_ITEM(ITEM_BOMB, 0, DrawItem),        ITEM_TRACKER_ITEM(ITEM_BOW, 0, DrawItem),
    ITEM_TRACKER_ITEM(ITEM_ARROW_FIRE, 0, DrawItem),  ITEM_TRACKER_ITEM(ITEM_DINS_FIRE, 0, DrawItem),
    ITEM_TRACKER_ITEM(ITEM_SLINGSHOT, 0, DrawItem),   ITEM_TRACKER_ITEM(ITEM_OCARINA_FAIRY, 0, DrawItem),
    ITEM_TRACKER_ITEM(ITEM_BOMBCHU, 0, DrawItem),     ITEM_TRACKER_ITEM(ITEM_HOOKSHOT, 0, DrawItem),
    ITEM_TRACKER_ITEM(ITEM_ARROW_ICE, 0, DrawItem),   ITEM_TRACKER_ITEM(ITEM_FARORES_WIND, 0, DrawItem),
    ITEM_TRACKER_ITEM(ITEM_BOOMERANG, 0, DrawItem),   ITEM_TRACKER_ITEM(ITEM_LENS, 0, DrawItem),
    ITEM_TRACKER_ITEM(ITEM_BEAN, 0, DrawItem),        ITEM_TRACKER_ITEM(ITEM_HAMMER, 0, DrawItem),
    ITEM_TRACKER_ITEM(ITEM_ARROW_LIGHT, 0, DrawItem), ITEM_TRACKER_ITEM(ITEM_NAYRUS_LOVE, 0, DrawItem),
    ITEM_TRACKER_ITEM(ITEM_BOTTLE, 0, DrawBottle),    ITEM_TRACKER_ITEM(ITEM_BOTTLE, 1, DrawBottle),
    ITEM_TRACKER_ITEM(ITEM_BOTTLE, 2, DrawBottle),    ITEM_TRACKER_ITEM(ITEM_BOTTLE, 3, DrawBottle),
    ITEM_TRACKER_ITEM(ITEM_POCKET_EGG, 0, DrawItem),  ITEM_TRACKER_ITEM(ITEM_MASK_KEATON, 0, DrawItem),
};

std::vector<ItemTrackerItem> equipmentItems = {
    ITEM_TRACKER_ITEM(ITEM_SWORD_KOKIRI, 1 << 0, DrawEquip),  ITEM_TRACKER_ITEM(ITEM_SWORD_MASTER, 1 << 1, DrawEquip),
    ITEM_TRACKER_ITEM(ITEM_SWORD_BGS, 1 << 2, DrawEquip),     ITEM_TRACKER_ITEM(ITEM_TUNIC_KOKIRI, 1 << 8, DrawEquip),
    ITEM_TRACKER_ITEM(ITEM_TUNIC_GORON, 1 << 9, DrawEquip),   ITEM_TRACKER_ITEM(ITEM_TUNIC_ZORA, 1 << 10, DrawEquip),
    ITEM_TRACKER_ITEM(ITEM_SHIELD_DEKU, 1 << 4, DrawEquip),   ITEM_TRACKER_ITEM(ITEM_SHIELD_HYLIAN, 1 << 5, DrawEquip),
    ITEM_TRACKER_ITEM(ITEM_SHIELD_MIRROR, 1 << 6, DrawEquip), ITEM_TRACKER_ITEM(ITEM_BOOTS_KOKIRI, 1 << 12, DrawEquip),
    ITEM_TRACKER_ITEM(ITEM_BOOTS_IRON, 1 << 13, DrawEquip),   ITEM_TRACKER_ITEM(ITEM_BOOTS_HOVER, 1 << 14, DrawEquip),
};

std::vector<ItemTrackerItem> miscItems = {
    ITEM_TRACKER_ITEM(ITEM_BRACELET, 0, DrawItem),
    ITEM_TRACKER_ITEM(ITEM_SCALE_SILVER, 0, DrawItem),
    ITEM_TRACKER_ITEM(ITEM_WALLET_ADULT, 0, DrawItem),
    ITEM_TRACKER_ITEM(ITEM_HEART_CONTAINER, 0, DrawItem),
    ITEM_TRACKER_ITEM(ITEM_HEART_PIECE, 0, DrawItem),
    ITEM_TRACKER_ITEM(ITEM_MAGIC_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM(QUEST_GERUDO_CARD, 1 << 22, DrawQuest),
    ITEM_TRACKER_ITEM(QUEST_SKULL_TOKEN, 1 << 23, DrawQuest),
    ITEM_TRACKER_ITEM(QUEST_STONE_OF_AGONY, 1 << 21, DrawQuest),
};

std::vector<ItemTrackerItem> dungeonRewardStones = {
    ITEM_TRACKER_ITEM(QUEST_KOKIRI_EMERALD, 1 << 18, DrawQuest),
    ITEM_TRACKER_ITEM(QUEST_GORON_RUBY, 1 << 19, DrawQuest),
    ITEM_TRACKER_ITEM(QUEST_ZORA_SAPPHIRE, 1 << 20, DrawQuest),
};

std::vector<ItemTrackerItem> dungeonRewardMedallions = {
    ITEM_TRACKER_ITEM(QUEST_MEDALLION_FOREST, 1 << 0, DrawQuest),
    ITEM_TRACKER_ITEM(QUEST_MEDALLION_FIRE, 1 << 1, DrawQuest),
    ITEM_TRACKER_ITEM(QUEST_MEDALLION_WATER, 1 << 2, DrawQuest),
    ITEM_TRACKER_ITEM(QUEST_MEDALLION_SPIRIT, 1 << 3, DrawQuest),
    ITEM_TRACKER_ITEM(QUEST_MEDALLION_SHADOW, 1 << 4, DrawQuest),
    ITEM_TRACKER_ITEM(QUEST_MEDALLION_LIGHT, 1 << 5, DrawQuest),
};

std::vector<ItemTrackerItem> dungeonRewards = {};

std::vector<ItemTrackerItem> songItems = {
    ITEM_TRACKER_ITEM(QUEST_SONG_LULLABY, 0, DrawSong),  ITEM_TRACKER_ITEM(QUEST_SONG_EPONA, 0, DrawSong),
    ITEM_TRACKER_ITEM(QUEST_SONG_SARIA, 0, DrawSong),    ITEM_TRACKER_ITEM(QUEST_SONG_SUN, 0, DrawSong),
    ITEM_TRACKER_ITEM(QUEST_SONG_TIME, 0, DrawSong),     ITEM_TRACKER_ITEM(QUEST_SONG_STORMS, 0, DrawSong),
    ITEM_TRACKER_ITEM(QUEST_SONG_MINUET, 0, DrawSong),   ITEM_TRACKER_ITEM(QUEST_SONG_BOLERO, 0, DrawSong),
    ITEM_TRACKER_ITEM(QUEST_SONG_SERENADE, 0, DrawSong), ITEM_TRACKER_ITEM(QUEST_SONG_REQUIEM, 0, DrawSong),
    ITEM_TRACKER_ITEM(QUEST_SONG_NOCTURNE, 0, DrawSong), ITEM_TRACKER_ITEM(QUEST_SONG_PRELUDE, 0, DrawSong),
};

std::vector<ItemTrackerItem> gregItems = {
    ITEM_TRACKER_ITEM(ITEM_RUPEE_GREEN, 0, DrawItem),
};

std::vector<ItemTrackerItem> triforcePieces = {
    ITEM_TRACKER_ITEM(RG_TRIFORCE_PIECE, 0, DrawItem),
};

std::vector<ItemTrackerItem> rocsFeather = {
    ITEM_TRACKER_ITEM(RG_ROCS_FEATHER, 0, DrawItem),
};

std::vector<ItemTrackerItem> swimItems = {
    ITEM_TRACKER_ITEM_CUSTOM(RG_BRONZE_SCALE, ITEM_SCALE_SILVER, ITEM_SCALE_SILVER, 0, DrawItem),
};

std::vector<ItemTrackerItem> crawlItems = {
    ITEM_TRACKER_ITEM(RG_CRAWL, 0, DrawItem),
};

std::vector<ItemTrackerItem> climbItems = {
    ITEM_TRACKER_ITEM(RG_CLIMB, 0, DrawItem),
};

std::vector<ItemTrackerItem> grabItems = {
    ITEM_TRACKER_ITEM(RG_POWER_BRACELET, 0, DrawItem),
};

std::vector<ItemTrackerItem> openChestItems = {
    ITEM_TRACKER_ITEM(RG_OPEN_CHEST, 0, DrawItem),
};

std::vector<ItemTrackerItem> beanSoulItems = {
    ITEM_TRACKER_ITEM_CUSTOM(RG_DEATH_MOUNTAIN_CRATER_BEAN_SOUL, ITEM_BEAN, ITEM_BEAN, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_DEATH_MOUNTAIN_TRAIL_BEAN_SOUL, ITEM_BEAN, ITEM_BEAN, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_DESERT_COLOSSUS_BEAN_SOUL, ITEM_BEAN, ITEM_BEAN, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_GERUDO_VALLEY_BEAN_SOUL, ITEM_BEAN, ITEM_BEAN, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_GRAVEYARD_BEAN_SOUL, ITEM_BEAN, ITEM_BEAN, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_KOKIRI_FOREST_BEAN_SOUL, ITEM_BEAN, ITEM_BEAN, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_LAKE_HYLIA_BEAN_SOUL, ITEM_BEAN, ITEM_BEAN, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_LOST_WOODS_BRIDGE_BEAN_SOUL, ITEM_BEAN, ITEM_BEAN, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_LOST_WOODS_BEAN_SOUL, ITEM_BEAN, ITEM_BEAN, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_ZORAS_RIVER_BEAN_SOUL, ITEM_BEAN, ITEM_BEAN, 0, DrawItem),
};

std::vector<ItemTrackerItem> bossSoulItems = {
    ITEM_TRACKER_ITEM(RG_GOHMA_SOUL, 0, DrawItem),       ITEM_TRACKER_ITEM(RG_KING_DODONGO_SOUL, 0, DrawItem),
    ITEM_TRACKER_ITEM(RG_BARINADE_SOUL, 0, DrawItem),    ITEM_TRACKER_ITEM(RG_PHANTOM_GANON_SOUL, 0, DrawItem),
    ITEM_TRACKER_ITEM(RG_VOLVAGIA_SOUL, 0, DrawItem),    ITEM_TRACKER_ITEM(RG_MORPHA_SOUL, 0, DrawItem),
    ITEM_TRACKER_ITEM(RG_BONGO_BONGO_SOUL, 0, DrawItem), ITEM_TRACKER_ITEM(RG_TWINROVA_SOUL, 0, DrawItem),
    ITEM_TRACKER_ITEM(RG_GANON_SOUL, 0, DrawItem),
};

std::vector<ItemTrackerItem> jabbernutItems = {
    ITEM_TRACKER_ITEM(RG_SPEAK_DEKU, 0, DrawItem),   ITEM_TRACKER_ITEM(RG_SPEAK_GERUDO, 0, DrawItem),
    ITEM_TRACKER_ITEM(RG_SPEAK_GORON, 0, DrawItem),  ITEM_TRACKER_ITEM(RG_SPEAK_HYLIAN, 0, DrawItem),
    ITEM_TRACKER_ITEM(RG_SPEAK_KOKIRI, 0, DrawItem), ITEM_TRACKER_ITEM(RG_SPEAK_ZORA, 0, DrawItem),
};

std::vector<ItemTrackerItem> ocarinaButtonItems = {
    // Hack for right now, just gonna draw ocarina buttons as ocarinas.
    // Will replace with other macro once we have a custom texture
    ITEM_TRACKER_ITEM_CUSTOM(RG_OCARINA_A_BUTTON, ITEM_OCARINA_TIME, ITEM_OCARINA_TIME, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_OCARINA_C_UP_BUTTON, ITEM_OCARINA_TIME, ITEM_OCARINA_TIME, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_OCARINA_C_DOWN_BUTTON, ITEM_OCARINA_TIME, ITEM_OCARINA_TIME, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_OCARINA_C_LEFT_BUTTON, ITEM_OCARINA_TIME, ITEM_OCARINA_TIME, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_OCARINA_C_RIGHT_BUTTON, ITEM_OCARINA_TIME, ITEM_OCARINA_TIME, 0, DrawItem),
};

std::vector<ItemTrackerItem> overworldKeyItems = {
    // Hack for right now, just gonna overworld keys as dungeon keys.
    // Will replace with other macro once we have a custom texture
    ITEM_TRACKER_ITEM_CUSTOM(RG_GUARD_HOUSE_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_MARKET_BAZAAR_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_MARKET_POTION_SHOP_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_MASK_SHOP_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_MARKET_SHOOTING_GALLERY_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_BOMBCHU_BOWLING_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_TREASURE_CHEST_GAME_BUILDING_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_BOMBCHU_SHOP_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_RICHARDS_HOUSE_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_ALLEY_HOUSE_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_KAK_BAZAAR_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_KAK_POTION_SHOP_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_BOSS_HOUSE_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_GRANNYS_POTION_SHOP_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_SKULLTULA_HOUSE_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_IMPAS_HOUSE_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_WINDMILL_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_KAK_SHOOTING_GALLERY_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_DAMPES_HUT_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_TALONS_HOUSE_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_STABLES_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_BACK_TOWER_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_HYLIA_LAB_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
    ITEM_TRACKER_ITEM_CUSTOM(RG_FISHING_HOLE_KEY, ITEM_KEY_SMALL, ITEM_KEY_SMALL, 0, DrawItem),
};

std::vector<ItemTrackerItem> fishingPoleItems = { ITEM_TRACKER_ITEM(ITEM_FISHING_POLE, 0, DrawItem) };

std::vector<ItemTrackerDungeon> itemTrackerDungeonsWithMapsHorizontal = {
    { SCENE_DEKU_TREE, { ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_DODONGOS_CAVERN, { ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_JABU_JABU, { ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_FOREST_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS, ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_FIRE_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS, ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_WATER_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS, ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_SPIRIT_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS, ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_SHADOW_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS, ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_INSIDE_GANONS_CASTLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS } },
    { SCENE_BOTTOM_OF_THE_WELL, { ITEM_KEY_SMALL, ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_ICE_CAVERN, { ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_GERUDO_TRAINING_GROUND, { ITEM_KEY_SMALL } },
};

std::vector<ItemTrackerDungeon> itemTrackerDungeonsHorizontal = {
    { SCENE_FOREST_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS } },
    { SCENE_FIRE_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS } },
    { SCENE_WATER_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS } },
    { SCENE_SPIRIT_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS } },
    { SCENE_SHADOW_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS } },
    { SCENE_INSIDE_GANONS_CASTLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS } },
    { SCENE_BOTTOM_OF_THE_WELL, { ITEM_KEY_SMALL } },
    { SCENE_GERUDO_TRAINING_GROUND, { ITEM_KEY_SMALL } },
};

std::vector<ItemTrackerDungeon> itemTrackerDungeonsWithMapsCompact = {
    { SCENE_FOREST_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS, ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_FIRE_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS, ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_WATER_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS, ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_SPIRIT_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS, ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_SHADOW_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS, ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_BOTTOM_OF_THE_WELL, { ITEM_KEY_SMALL, ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_DEKU_TREE, { ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_DODONGOS_CAVERN, { ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_JABU_JABU, { ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_ICE_CAVERN, { ITEM_DUNGEON_MAP, ITEM_COMPASS } },
    { SCENE_INSIDE_GANONS_CASTLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS } },
    { SCENE_GERUDO_TRAINING_GROUND, { ITEM_KEY_SMALL } },
};

std::vector<ItemTrackerDungeon> itemTrackerDungeonsCompact = {
    { SCENE_FOREST_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS } },
    { SCENE_FIRE_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS } },
    { SCENE_WATER_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS } },
    { SCENE_SPIRIT_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS } },
    { SCENE_SHADOW_TEMPLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS } },
    { SCENE_INSIDE_GANONS_CASTLE, { ITEM_KEY_SMALL, ITEM_KEY_BOSS } },
    { SCENE_BOTTOM_OF_THE_WELL, { ITEM_KEY_SMALL } },
    { SCENE_GERUDO_TRAINING_GROUND, { ITEM_KEY_SMALL } },
    { SCENE_THIEVES_HIDEOUT, { ITEM_KEY_SMALL } },
};

std::map<uint16_t, std::string> itemTrackerDungeonShortNames = {
    { SCENE_FOREST_TEMPLE, "FRST" },   { SCENE_FIRE_TEMPLE, "FIRE" },           { SCENE_WATER_TEMPLE, "WATR" },
    { SCENE_SPIRIT_TEMPLE, "SPRT" },   { SCENE_SHADOW_TEMPLE, "SHDW" },         { SCENE_BOTTOM_OF_THE_WELL, "BOTW" },
    { SCENE_DEKU_TREE, "DEKU" },       { SCENE_DODONGOS_CAVERN, "DCVN" },       { SCENE_JABU_JABU, "JABU" },
    { SCENE_ICE_CAVERN, "ICE" },       { SCENE_INSIDE_GANONS_CASTLE, "GANON" }, { SCENE_GERUDO_TRAINING_GROUND, "GTG" },
    { SCENE_THIEVES_HIDEOUT, "HIDE" },
};

std::map<uint16_t, std::string> itemTrackerBeanShortNames = {
    { RG_DEATH_MOUNTAIN_CRATER_BEAN_SOUL, "DMC" },
    { RG_DEATH_MOUNTAIN_TRAIL_BEAN_SOUL, "DMT" },
    { RG_DESERT_COLOSSUS_BEAN_SOUL, "DC" },
    { RG_GERUDO_VALLEY_BEAN_SOUL, "GV" },
    { RG_GRAVEYARD_BEAN_SOUL, "GY" },
    { RG_KOKIRI_FOREST_BEAN_SOUL, "KF" },
    { RG_LAKE_HYLIA_BEAN_SOUL, "LH" },
    { RG_LOST_WOODS_BRIDGE_BEAN_SOUL, "LWB" },
    { RG_LOST_WOODS_BEAN_SOUL, "LWT" },
    { RG_ZORAS_RIVER_BEAN_SOUL, "ZR" },
};

std::map<uint16_t, std::string> itemTrackerBossShortNames = {
    { RG_GOHMA_SOUL, "GOHMA" },       { RG_KING_DODONGO_SOUL, "KD" }, { RG_BARINADE_SOUL, "BARI" },
    { RG_PHANTOM_GANON_SOUL, "PG" },  { RG_VOLVAGIA_SOUL, "VOLV" },   { RG_MORPHA_SOUL, "MORPH" },
    { RG_BONGO_BONGO_SOUL, "BONGO" }, { RG_TWINROVA_SOUL, "TWIN" },   { RG_GANON_SOUL, "GANON" },
};

std::map<uint16_t, std::string> itemTrackerJabberNutShortNames = {
    { RG_SPEAK_DEKU, "DEKU" },     { RG_SPEAK_GERUDO, "GERUDO" }, { RG_SPEAK_GORON, "GORON" },
    { RG_SPEAK_HYLIAN, "HYLIAN" }, { RG_SPEAK_KOKIRI, "KOKIRI" }, { RG_SPEAK_ZORA, "ZORA" },
};

std::map<uint16_t, std::string> itemTrackerOcarinaButtonShortNames = {
    { RG_OCARINA_A_BUTTON, "A" },        { RG_OCARINA_C_UP_BUTTON, "C-U" },    { RG_OCARINA_C_DOWN_BUTTON, "C-D" },
    { RG_OCARINA_C_LEFT_BUTTON, "C-L" }, { RG_OCARINA_C_RIGHT_BUTTON, "C-R" },
};

std::map<uint16_t, std::string> itemTrackerOverworldKeyShortNames = {
    { RG_GUARD_HOUSE_KEY, "GUARD" },
    { RG_MARKET_BAZAAR_KEY, "MKBAZ" },
    { RG_MARKET_POTION_SHOP_KEY, "MKPOT" },
    { RG_MASK_SHOP_KEY, "MASK" },
    { RG_MARKET_SHOOTING_GALLERY_KEY, "MKSHO" },
    { RG_BOMBCHU_BOWLING_KEY, "BOWL" },
    { RG_TREASURE_CHEST_GAME_BUILDING_KEY, "TREASU" },
    { RG_BOMBCHU_SHOP_KEY, "CHUSHO" },
    { RG_RICHARDS_HOUSE_KEY, "RICH" },
    { RG_ALLEY_HOUSE_KEY, "ALLEY" },
    { RG_KAK_BAZAAR_KEY, "KAKBAZ" },
    { RG_KAK_POTION_SHOP_KEY, "KAKPO" },
    { RG_BOSS_HOUSE_KEY, "BOSS" },
    { RG_GRANNYS_POTION_SHOP_KEY, "GRANNY" },
    { RG_SKULLTULA_HOUSE_KEY, "SKULL" },
    { RG_IMPAS_HOUSE_KEY, "IMPAS" },
    { RG_WINDMILL_KEY, "WIND" },
    { RG_KAK_SHOOTING_GALLERY_KEY, "KAKSHO" },
    { RG_DAMPES_HUT_KEY, "DAMPES" },
    { RG_TALONS_HOUSE_KEY, "TALONS" },
    { RG_STABLES_KEY, "STABLE" },
    { RG_BACK_TOWER_KEY, "TOWER" },
    { RG_HYLIA_LAB_KEY, "LAB" },
    { RG_FISHING_HOLE_KEY, "FISH" },
};

std::vector<ItemTrackerItem> dungeonItems = {};

std::unordered_map<uint32_t, ItemTrackerItem> actualItemTrackerItemMap = {
    { ITEM_BOTTLE, ITEM_TRACKER_ITEM(ITEM_BOTTLE, 0, DrawItem) },
    { ITEM_BIG_POE, ITEM_TRACKER_ITEM(ITEM_BIG_POE, 0, DrawItem) },
    { ITEM_BLUE_FIRE, ITEM_TRACKER_ITEM(ITEM_BLUE_FIRE, 0, DrawItem) },
    { ITEM_BUG, ITEM_TRACKER_ITEM(ITEM_BUG, 0, DrawItem) },
    { ITEM_FAIRY, ITEM_TRACKER_ITEM(ITEM_FAIRY, 0, DrawItem) },
    { ITEM_FISH, ITEM_TRACKER_ITEM(ITEM_FISH, 0, DrawItem) },
    { ITEM_POTION_GREEN, ITEM_TRACKER_ITEM(ITEM_POTION_GREEN, 0, DrawItem) },
    { ITEM_POE, ITEM_TRACKER_ITEM(ITEM_POE, 0, DrawItem) },
    { ITEM_POTION_RED, ITEM_TRACKER_ITEM(ITEM_POTION_RED, 0, DrawItem) },
    { ITEM_POTION_BLUE, ITEM_TRACKER_ITEM(ITEM_POTION_BLUE, 0, DrawItem) },
    { ITEM_MILK_BOTTLE, ITEM_TRACKER_ITEM(ITEM_MILK_BOTTLE, 0, DrawItem) },
    { ITEM_MILK_HALF, ITEM_TRACKER_ITEM(ITEM_MILK_HALF, 0, DrawItem) },
    { ITEM_LETTER_RUTO, ITEM_TRACKER_ITEM(ITEM_LETTER_RUTO, 0, DrawItem) },

    { ITEM_HOOKSHOT, ITEM_TRACKER_ITEM(ITEM_HOOKSHOT, 0, DrawItem) },
    { ITEM_LONGSHOT, ITEM_TRACKER_ITEM(ITEM_LONGSHOT, 0, DrawItem) },

    { ITEM_OCARINA_FAIRY, ITEM_TRACKER_ITEM(ITEM_OCARINA_FAIRY, 0, DrawItem) },
    { ITEM_OCARINA_TIME, ITEM_TRACKER_ITEM(ITEM_OCARINA_TIME, 0, DrawItem) },

    { ITEM_MAGIC_SMALL, ITEM_TRACKER_ITEM(ITEM_MAGIC_SMALL, 0, DrawItem) },
    { ITEM_MAGIC_LARGE, ITEM_TRACKER_ITEM(ITEM_MAGIC_LARGE, 0, DrawItem) },

    { ITEM_WALLET_ADULT, ITEM_TRACKER_ITEM(ITEM_WALLET_ADULT, 0, DrawItem) },
    { ITEM_WALLET_GIANT, ITEM_TRACKER_ITEM(ITEM_WALLET_GIANT, 0, DrawItem) },

    { ITEM_BRACELET, ITEM_TRACKER_ITEM(ITEM_BRACELET, 0, DrawItem) },
    { ITEM_GAUNTLETS_SILVER, ITEM_TRACKER_ITEM(ITEM_GAUNTLETS_SILVER, 0, DrawItem) },
    { ITEM_GAUNTLETS_GOLD, ITEM_TRACKER_ITEM(ITEM_GAUNTLETS_GOLD, 0, DrawItem) },

    { ITEM_SCALE_SILVER, ITEM_TRACKER_ITEM(ITEM_SCALE_SILVER, 0, DrawItem) },
    { ITEM_SCALE_GOLDEN, ITEM_TRACKER_ITEM(ITEM_SCALE_GOLDEN, 0, DrawItem) },

    { ITEM_WEIRD_EGG, ITEM_TRACKER_ITEM(ITEM_WEIRD_EGG, 0, DrawItem) },
    { ITEM_CHICKEN, ITEM_TRACKER_ITEM(ITEM_CHICKEN, 0, DrawItem) },
    { ITEM_LETTER_ZELDA, ITEM_TRACKER_ITEM(ITEM_LETTER_ZELDA, 0, DrawItem) },
    { ITEM_MASK_KEATON, ITEM_TRACKER_ITEM(ITEM_MASK_KEATON, 0, DrawItem) },
    { ITEM_MASK_SKULL, ITEM_TRACKER_ITEM(ITEM_MASK_SKULL, 0, DrawItem) },
    { ITEM_MASK_SPOOKY, ITEM_TRACKER_ITEM(ITEM_MASK_SPOOKY, 0, DrawItem) },
    { ITEM_MASK_BUNNY, ITEM_TRACKER_ITEM(ITEM_MASK_BUNNY, 0, DrawItem) },
    { ITEM_MASK_GORON, ITEM_TRACKER_ITEM(ITEM_MASK_GORON, 0, DrawItem) },
    { ITEM_MASK_ZORA, ITEM_TRACKER_ITEM(ITEM_MASK_ZORA, 0, DrawItem) },
    { ITEM_MASK_GERUDO, ITEM_TRACKER_ITEM(ITEM_MASK_GERUDO, 0, DrawItem) },
    { ITEM_MASK_TRUTH, ITEM_TRACKER_ITEM(ITEM_MASK_TRUTH, 0, DrawItem) },
    { ITEM_SOLD_OUT, ITEM_TRACKER_ITEM(ITEM_SOLD_OUT, 0, DrawItem) },

    { ITEM_POCKET_EGG, ITEM_TRACKER_ITEM(ITEM_POCKET_EGG, 0, DrawItem) },
    { ITEM_POCKET_CUCCO, ITEM_TRACKER_ITEM(ITEM_POCKET_CUCCO, 0, DrawItem) },
    { ITEM_COJIRO, ITEM_TRACKER_ITEM(ITEM_COJIRO, 0, DrawItem) },
    { ITEM_ODD_MUSHROOM, ITEM_TRACKER_ITEM(ITEM_ODD_MUSHROOM, 0, DrawItem) },
    { ITEM_ODD_POTION, ITEM_TRACKER_ITEM(ITEM_ODD_POTION, 0, DrawItem) },
    { ITEM_SAW, ITEM_TRACKER_ITEM(ITEM_SAW, 0, DrawItem) },
    { ITEM_SWORD_BROKEN, ITEM_TRACKER_ITEM(ITEM_SWORD_BROKEN, 0, DrawItem) },
    { ITEM_PRESCRIPTION, ITEM_TRACKER_ITEM(ITEM_PRESCRIPTION, 0, DrawItem) },
    { ITEM_FROG, ITEM_TRACKER_ITEM(ITEM_FROG, 0, DrawItem) },
    { ITEM_EYEDROPS, ITEM_TRACKER_ITEM(ITEM_EYEDROPS, 0, DrawItem) },
    { ITEM_CLAIM_CHECK, ITEM_TRACKER_ITEM(ITEM_CLAIM_CHECK, 0, DrawItem) },
};

std::vector<uint32_t> buttonMap = {
    BTN_A, BTN_B, BTN_CUP,   BTN_CDOWN, BTN_CLEFT, BTN_CRIGHT, BTN_L,
    BTN_Z, BTN_R, BTN_START, BTN_DUP,   BTN_DDOWN, BTN_DLEFT,  BTN_DRIGHT,
};

bool IsValidSaveFile() {
    bool validSave = gSaveContext.fileNum >= 0 && gSaveContext.fileNum <= 2;
    return validSave;
}

bool HasSong(ItemTrackerItem item) {
    return GameInteractor::IsSaveLoaded() ? ((1 << item.id) & gSaveContext.inventory.questItems) : false;
}

bool HasQuestItem(ItemTrackerItem item) {
    return GameInteractor::IsSaveLoaded() ? (item.data & gSaveContext.inventory.questItems) : false;
}

bool HasEquipment(ItemTrackerItem item) {
    return GameInteractor::IsSaveLoaded() ? (item.data & gSaveContext.inventory.equipment) : false;
}

ItemTrackerNumbers GetItemCurrentAndMax(ItemTrackerItem item) {
    ItemTrackerNumbers result;
    result.currentCapacity = 0;
    result.maxCapacity = 0;
    result.currentAmmo = 0;

    switch (item.id) {
        case ITEM_STICK:
            result.currentCapacity = CUR_CAPACITY(UPG_STICKS);
            result.maxCapacity = 30;
            result.currentAmmo = AMMO(ITEM_STICK);
            break;
        case ITEM_NUT:
            result.currentCapacity = CUR_CAPACITY(UPG_NUTS);
            result.maxCapacity = 40;
            result.currentAmmo = AMMO(ITEM_NUT);
            break;
        case ITEM_BOMB:
            result.currentCapacity = CUR_CAPACITY(UPG_BOMB_BAG);
            result.maxCapacity = 40;
            result.currentAmmo = AMMO(ITEM_BOMB);
            break;
        case ITEM_BOW:
            result.currentCapacity = CUR_CAPACITY(UPG_QUIVER);
            result.maxCapacity = 50;
            result.currentAmmo = AMMO(ITEM_BOW);
            break;
        case ITEM_SLINGSHOT:
            result.currentCapacity = CUR_CAPACITY(UPG_BULLET_BAG);
            result.maxCapacity = 50;
            result.currentAmmo = AMMO(ITEM_SLINGSHOT);
            break;
        case ITEM_WALLET_ADULT:
        case ITEM_WALLET_GIANT:
            result.currentCapacity =
                IS_RANDO && !Flags_GetRandomizerInf(RAND_INF_HAS_WALLET) ? 0 : CUR_CAPACITY(UPG_WALLET);
            result.maxCapacity =
                IS_RANDO && OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_INCLUDE_TYCOON_WALLET) ? 999
                                                                                                               : 500;
            result.currentAmmo = gSaveContext.rupees;
            break;
        case ITEM_BOMBCHU: {
            auto bombchuBag = RAND_GET_OPTION(RSK_BOMBCHU_BAG);

            uint8_t capacity = 0;

            if (INV_CONTENT(ITEM_BOMBCHU) == ITEM_BOMBCHU) {
                if (bombchuBag.Is(RO_BOMBCHU_BAG_PROGRESSIVE)) {
                    capacity = OTRGlobals::Instance->gRandoContext->GetBombchuCapacity();
                } else {
                    capacity = 50;
                }
            }

            result.currentCapacity = capacity;
            result.maxCapacity = 50;
            result.currentAmmo = AMMO(ITEM_BOMBCHU);
            break;
        }
        case ITEM_BEAN:
            result.currentCapacity = INV_CONTENT(ITEM_BEAN) == ITEM_BEAN ? 10 : 0;
            result.maxCapacity = 10;
            result.currentAmmo = AMMO(ITEM_BEAN);
            break;
        case QUEST_SKULL_TOKEN:
            result.maxCapacity = result.currentCapacity = 100;
            result.currentAmmo = gSaveContext.inventory.gsTokens;
            break;
        case ITEM_HEART_CONTAINER:
            result.maxCapacity = result.currentCapacity = 8;
            result.currentAmmo = gSaveContext.ship.stats.heartContainers;
            break;
        case ITEM_HEART_PIECE:
            result.maxCapacity = result.currentCapacity = 36;
            result.currentAmmo = gSaveContext.ship.stats.heartPieces;
            break;
        case ITEM_KEY_SMALL:
            // Though the ammo/capacity naming doesn't really make sense for keys, we are
            // hijacking the same system to display key counts as there are enough similarities
            result.currentAmmo = MAX(gSaveContext.inventory.dungeonKeys[item.data], 0);
            result.currentCapacity = gSaveContext.ship.stats.dungeonKeys[item.data];
            switch (item.data) {
                case SCENE_FOREST_TEMPLE:
                    result.maxCapacity = FOREST_TEMPLE_SMALL_KEY_MAX;
                    break;
                case SCENE_FIRE_TEMPLE:
                    result.maxCapacity = FIRE_TEMPLE_SMALL_KEY_MAX;
                    break;
                case SCENE_WATER_TEMPLE:
                    result.maxCapacity = WATER_TEMPLE_SMALL_KEY_MAX;
                    break;
                case SCENE_SPIRIT_TEMPLE:
                    result.maxCapacity = SPIRIT_TEMPLE_SMALL_KEY_MAX;
                    break;
                case SCENE_SHADOW_TEMPLE:
                    result.maxCapacity = SHADOW_TEMPLE_SMALL_KEY_MAX;
                    break;
                case SCENE_BOTTOM_OF_THE_WELL:
                    result.maxCapacity = BOTTOM_OF_THE_WELL_SMALL_KEY_MAX;
                    break;
                case SCENE_GERUDO_TRAINING_GROUND:
                    result.maxCapacity = GERUDO_TRAINING_GROUND_SMALL_KEY_MAX;
                    break;
                case SCENE_THIEVES_HIDEOUT:
                    if (IS_RANDO) {
                        switch (OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_GERUDO_FORTRESS)) {
                            case RO_GF_CARPENTERS_NORMAL:
                                result.maxCapacity = GERUDO_FORTRESS_SMALL_KEY_MAX;
                                break;
                            case RO_GF_CARPENTERS_FAST:
                                result.maxCapacity = 1;
                                break;
                            case RO_GF_CARPENTERS_FREE:
                                result.maxCapacity = 0;
                                break;
                            default:
                                result.maxCapacity = 0;
                                SPDLOG_ERROR(
                                    "Invalid value for RSK_GERUDO_FORTRESS: {}",
                                    OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_GERUDO_FORTRESS));
                                assert(false);
                                break;
                        }
                    } else {
                        result.maxCapacity = GERUDO_FORTRESS_SMALL_KEY_MAX;
                    }
                    break;
                case SCENE_INSIDE_GANONS_CASTLE:
                    result.maxCapacity = GANONS_CASTLE_SMALL_KEY_MAX;
                    break;
            }
            break;
    }

    return result;
}

std::vector<ItemTrackerItem> GetDungeonItemsVector(std::vector<ItemTrackerDungeon> dungeons, size_t columns = 6) {
    std::vector<ItemTrackerItem> dungeonItems = {};

    size_t rowCount = 0;
    for (size_t i = 0; i < dungeons.size(); i++) {
        if (dungeons[i].items.size() > rowCount)
            rowCount = static_cast<int32_t>(dungeons[i].items.size());
    }

    for (size_t i = 0; i < rowCount; i++) {
        for (size_t j = 0; j < MIN(dungeons.size(), columns); j++) {
            if (dungeons[j].items.size() > i) {
                switch (dungeons[j].items[i]) {
                    case ITEM_KEY_SMALL:
                        dungeonItems.push_back(ITEM_TRACKER_ITEM(ITEM_KEY_SMALL, dungeons[j].id, DrawDungeonItem));
                        break;
                    case ITEM_KEY_BOSS:
                        // Swap Ganon's Castle boss key to the right scene ID manually
                        if (dungeons[j].id == SCENE_INSIDE_GANONS_CASTLE) {
                            dungeonItems.push_back(
                                ITEM_TRACKER_ITEM(ITEM_KEY_BOSS, SCENE_GANONS_TOWER, DrawDungeonItem));
                        } else {
                            dungeonItems.push_back(ITEM_TRACKER_ITEM(ITEM_KEY_BOSS, dungeons[j].id, DrawDungeonItem));
                        }
                        break;
                    case ITEM_DUNGEON_MAP:
                        dungeonItems.push_back(ITEM_TRACKER_ITEM(ITEM_DUNGEON_MAP, dungeons[j].id, DrawDungeonItem));
                        break;
                    case ITEM_COMPASS:
                        dungeonItems.push_back(ITEM_TRACKER_ITEM(ITEM_COMPASS, dungeons[j].id, DrawDungeonItem));
                        break;
                }
            } else {
                dungeonItems.push_back(ITEM_TRACKER_ITEM(ITEM_NONE, 0, DrawItem));
            }
        }
    }

    if (dungeons.size() > columns) {
        std::vector<ItemTrackerItem> nextDungeonItems =
            GetDungeonItemsVector(std::vector<ItemTrackerDungeon>(dungeons.begin() + columns, dungeons.end()), columns);
        dungeonItems.insert(dungeonItems.end(), nextDungeonItems.begin(), nextDungeonItems.end());
    }

    return dungeonItems;
}
/* ****************************************************** */

void UpdateVectors() {
    if (!shouldUpdateVectors) {
        return;
    }

    dungeonRewards.clear();
    dungeonRewards.insert(dungeonRewards.end(), dungeonRewardStones.begin(), dungeonRewardStones.end());
    dungeonRewards.insert(dungeonRewards.end(), dungeonRewardMedallions.begin(), dungeonRewardMedallions.end());

    dungeonItems.clear();
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DungeonItems.Layout"), 1) &&
        CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.DungeonItems"), SECTION_DISPLAY_HIDDEN) ==
            SECTION_DISPLAY_SEPARATE) {
        if (CVarGetInteger(CVAR_TRACKER_ITEM("DungeonItems.DisplayMaps"), 1)) {
            dungeonItems = GetDungeonItemsVector(itemTrackerDungeonsWithMapsHorizontal, 12);
            // Manually adding Thieves Hideout to an open spot so we don't get an additional row for one item
            dungeonItems[23] = ITEM_TRACKER_ITEM(ITEM_KEY_SMALL, SCENE_THIEVES_HIDEOUT, DrawDungeonItem);
        } else {
            // Manually adding Thieves Hideout to an open spot so we don't get an additional row for one item
            dungeonItems = GetDungeonItemsVector(itemTrackerDungeonsHorizontal, 8);
            dungeonItems[15] = ITEM_TRACKER_ITEM(ITEM_KEY_SMALL, SCENE_THIEVES_HIDEOUT, DrawDungeonItem);
        }
    } else {
        if (CVarGetInteger(CVAR_TRACKER_ITEM("DungeonItems.DisplayMaps"), 1)) {
            dungeonItems = GetDungeonItemsVector(itemTrackerDungeonsWithMapsCompact);
            // Manually adding Thieves Hideout to an open spot so we don't get an additional row for one item
            dungeonItems[35] = ITEM_TRACKER_ITEM(ITEM_KEY_SMALL, SCENE_THIEVES_HIDEOUT, DrawDungeonItem);
        } else {
            dungeonItems = GetDungeonItemsVector(itemTrackerDungeonsCompact);
        }
    }

    mainWindowItems.clear();
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Inventory"), SECTION_DISPLAY_MAIN_WINDOW) ==
        SECTION_DISPLAY_MAIN_WINDOW) {
        mainWindowItems.insert(mainWindowItems.end(), inventoryItems.begin(), inventoryItems.end());
    }
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Equipment"), SECTION_DISPLAY_MAIN_WINDOW) ==
        SECTION_DISPLAY_MAIN_WINDOW) {
        mainWindowItems.insert(mainWindowItems.end(), equipmentItems.begin(), equipmentItems.end());
    }
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Misc"), SECTION_DISPLAY_MAIN_WINDOW) ==
        SECTION_DISPLAY_MAIN_WINDOW) {
        mainWindowItems.insert(mainWindowItems.end(), miscItems.begin(), miscItems.end());
    }
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.DungeonRewards"), SECTION_DISPLAY_MAIN_WINDOW) ==
        SECTION_DISPLAY_MAIN_WINDOW) {
        mainWindowItems.insert(mainWindowItems.end(), dungeonRewardStones.begin(), dungeonRewardStones.end());
        mainWindowItems.insert(mainWindowItems.end(), dungeonRewardMedallions.begin(), dungeonRewardMedallions.end());
    }
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Songs"), SECTION_DISPLAY_MAIN_WINDOW) ==
        SECTION_DISPLAY_MAIN_WINDOW) {
        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Misc"), SECTION_DISPLAY_MAIN_WINDOW) ==
                SECTION_DISPLAY_MAIN_WINDOW &&
            CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.DungeonRewards"), SECTION_DISPLAY_MAIN_WINDOW) !=
                SECTION_DISPLAY_MAIN_WINDOW) {
            mainWindowItems.push_back(ITEM_TRACKER_ITEM(ITEM_NONE, 0, DrawItem));
            mainWindowItems.push_back(ITEM_TRACKER_ITEM(ITEM_NONE, 0, DrawItem));
            mainWindowItems.push_back(ITEM_TRACKER_ITEM(ITEM_NONE, 0, DrawItem));
        }
        mainWindowItems.insert(mainWindowItems.end(), songItems.begin(), songItems.end());
    }
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.DungeonItems"), SECTION_DISPLAY_HIDDEN) ==
        SECTION_DISPLAY_MAIN_WINDOW) {
        mainWindowItems.insert(mainWindowItems.end(), dungeonItems.begin(), dungeonItems.end());
    }
    if (IS_RANDO && RAND_GET_OPTION(RSK_ROCS_FEATHER)) {
        mainWindowItems.insert(mainWindowItems.end(), rocsFeather.begin(), rocsFeather.end());
    }
    if (IS_RANDO && RAND_GET_OPTION(RSK_SHUFFLE_SWIM)) {
        mainWindowItems.insert(mainWindowItems.end(), swimItems.begin(), swimItems.end());
    }
    if (IS_RANDO && RAND_GET_OPTION(RSK_SHUFFLE_GRAB)) {
        mainWindowItems.insert(mainWindowItems.end(), grabItems.begin(), grabItems.end());
    }
    if (IS_RANDO && RAND_GET_OPTION(RSK_SHUFFLE_CLIMB)) {
        mainWindowItems.insert(mainWindowItems.end(), climbItems.begin(), climbItems.end());
    }
    if (IS_RANDO && RAND_GET_OPTION(RSK_SHUFFLE_CRAWL)) {
        mainWindowItems.insert(mainWindowItems.end(), crawlItems.begin(), crawlItems.end());
    }
    if (IS_RANDO && RAND_GET_OPTION(RSK_SHUFFLE_OPEN_CHEST)) {
        mainWindowItems.insert(mainWindowItems.end(), openChestItems.begin(), openChestItems.end());
    }

    // if we're adding greg to the misc window,
    // and misc isn't on the main window,
    // and it doesn't already have greg, add him
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Greg"), SECTION_DISPLAY_EXTENDED_HIDDEN) ==
            SECTION_DISPLAY_EXTENDED_MISC_WINDOW &&
        CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Misc"), SECTION_DISPLAY_MAIN_WINDOW) !=
            SECTION_DISPLAY_MAIN_WINDOW) {
        if (std::none_of(miscItems.begin(), miscItems.end(),
                         [](ItemTrackerItem item) { return item.id == ITEM_RUPEE_GREEN; }))
            miscItems.insert(miscItems.end(), gregItems.begin(), gregItems.end());
    } else {
        miscItems.erase(std::remove_if(miscItems.begin(), miscItems.end(),
                                       [](ItemTrackerItem i) { return i.id == ITEM_RUPEE_GREEN; }),
                        miscItems.end());
    }

    bool newRowAdded = false;
    // if we're adding greg to the main window
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Greg"), SECTION_DISPLAY_EXTENDED_HIDDEN) ==
        SECTION_DISPLAY_EXTENDED_MAIN_WINDOW) {
        if (!newRowAdded) {
            // insert empty items until we're on a new row for greg
            while (mainWindowItems.size() % 6) {
                mainWindowItems.push_back(ITEM_TRACKER_ITEM(ITEM_NONE, 0, DrawItem));
            }
            newRowAdded = true;
        }

        // add greg
        mainWindowItems.insert(mainWindowItems.end(), gregItems.begin(), gregItems.end());
    }

    // If we're adding triforce pieces to the main window
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.TriforcePieces"), SECTION_DISPLAY_HIDDEN) ==
        SECTION_DISPLAY_MAIN_WINDOW) {
        // If Greg isn't on the main window, add empty items to place the triforce pieces on a new row.
        if (!newRowAdded) {
            while (mainWindowItems.size() % 6) {
                mainWindowItems.push_back(ITEM_TRACKER_ITEM(ITEM_NONE, 0, DrawItem));
            }
            newRowAdded = true;
        }

        // Add triforce pieces
        mainWindowItems.insert(mainWindowItems.end(), triforcePieces.begin(), triforcePieces.end());
    }

    // if misc is separate and fishing pole isn't added, add fishing pole to misc
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.FishingPole"), SECTION_DISPLAY_EXTENDED_HIDDEN) ==
            SECTION_DISPLAY_EXTENDED_MISC_WINDOW &&
        CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Misc"), SECTION_DISPLAY_MAIN_WINDOW) !=
            SECTION_DISPLAY_MAIN_WINDOW) {
        if (std::none_of(miscItems.begin(), miscItems.end(),
                         [](ItemTrackerItem item) { return item.id == ITEM_FISHING_POLE; }))
            miscItems.insert(miscItems.end(), fishingPoleItems.begin(), fishingPoleItems.end());
    } else {
        miscItems.erase(std::remove_if(miscItems.begin(), miscItems.end(),
                                       [](ItemTrackerItem i) { return i.id == ITEM_FISHING_POLE; }),
                        miscItems.end());
    }
    // add fishing pole to main window
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.FishingPole"), SECTION_DISPLAY_EXTENDED_HIDDEN) ==
        SECTION_DISPLAY_EXTENDED_MAIN_WINDOW) {
        if (!newRowAdded) {
            while (mainWindowItems.size() % 6) {
                mainWindowItems.push_back(ITEM_TRACKER_ITEM(ITEM_NONE, 0, DrawItem));
            }
            newRowAdded = true;
        }

        mainWindowItems.insert(mainWindowItems.end(), fishingPoleItems.begin(), fishingPoleItems.end());
    }

    // If we're adding bean souls to the main window...
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.BeanSouls"), SECTION_DISPLAY_HIDDEN) ==
        SECTION_DISPLAY_MAIN_WINDOW) {
        //...add empty items on the main window to get the souls on their own row. (Too many to sit with Greg/Triforce
        // pieces)
        while (mainWindowItems.size() % 6) {
            mainWindowItems.push_back(ITEM_TRACKER_ITEM(ITEM_NONE, 0, DrawItem));
        }

        // Add bean souls
        mainWindowItems.insert(mainWindowItems.end(), beanSoulItems.begin(), beanSoulItems.end());
    }

    // If we're adding boss souls to the main window...
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.BossSouls"), SECTION_DISPLAY_HIDDEN) ==
        SECTION_DISPLAY_MAIN_WINDOW) {
        //...add empty items on the main window to get the souls on their own row
        // (Too many to sit with Greg/Triforce pieces)
        while (mainWindowItems.size() % 6) {
            mainWindowItems.push_back(ITEM_TRACKER_ITEM(ITEM_NONE, 0, DrawItem));
        }

        // Add boss souls
        mainWindowItems.insert(mainWindowItems.end(), bossSoulItems.begin(), bossSoulItems.end());
    }

    // If we're adding jabbernuts to the main window...
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.JabberNuts"), SECTION_DISPLAY_HIDDEN) ==
        SECTION_DISPLAY_MAIN_WINDOW) {
        // there are 6 jabbernuts, perfect for a row
        while (mainWindowItems.size() % 6) {
            mainWindowItems.push_back(ITEM_TRACKER_ITEM(ITEM_NONE, 0, DrawItem));
        }

        // Add jabbernuts
        mainWindowItems.insert(mainWindowItems.end(), jabbernutItems.begin(), jabbernutItems.end());
    }

    // If we're adding ocarina buttons to the main window...
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.OcarinaButtons"), SECTION_DISPLAY_HIDDEN) ==
        SECTION_DISPLAY_MAIN_WINDOW) {
        //...add empty items on the main window to get the buttons on their own row.
        // (Too many to sit with Greg/Triforce pieces/boss souls)
        while (mainWindowItems.size() % 6) {
            mainWindowItems.push_back(ITEM_TRACKER_ITEM(ITEM_NONE, 0, DrawItem));
        }

        // Add ocarina buttons
        mainWindowItems.insert(mainWindowItems.end(), ocarinaButtonItems.begin(), ocarinaButtonItems.end());
    }

    // If we're adding overworld keys to the main window...
    if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.OverworldKeys"), SECTION_DISPLAY_HIDDEN) ==
        SECTION_DISPLAY_MAIN_WINDOW) {
        //...add empty items on the main window to get the keys on their own row.
        // (Too many to sit with Greg/Triforce pieces/boss souls/ocarina buttons)
        while (mainWindowItems.size() % 6) {
            mainWindowItems.push_back(ITEM_TRACKER_ITEM(ITEM_NONE, 0, DrawItem));
        }

        // Add overworld keys
        mainWindowItems.insert(mainWindowItems.end(), overworldKeyItems.begin(), overworldKeyItems.end());
    }

    shouldUpdateVectors = false;
}
