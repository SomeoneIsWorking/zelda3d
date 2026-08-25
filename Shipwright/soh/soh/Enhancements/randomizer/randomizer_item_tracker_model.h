#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "randomizer_item_tracker.h"

extern bool shouldUpdateVectors;

extern std::vector<ItemTrackerItem> mainWindowItems;
extern std::vector<ItemTrackerItem> inventoryItems;
extern std::vector<ItemTrackerItem> equipmentItems;
extern std::vector<ItemTrackerItem> miscItems;
extern std::vector<ItemTrackerItem> dungeonRewardStones;
extern std::vector<ItemTrackerItem> dungeonRewardMedallions;
extern std::vector<ItemTrackerItem> dungeonRewards;
extern std::vector<ItemTrackerItem> songItems;
extern std::vector<ItemTrackerItem> gregItems;
extern std::vector<ItemTrackerItem> triforcePieces;
extern std::vector<ItemTrackerItem> rocsFeather;
extern std::vector<ItemTrackerItem> swimItems;
extern std::vector<ItemTrackerItem> crawlItems;
extern std::vector<ItemTrackerItem> climbItems;
extern std::vector<ItemTrackerItem> grabItems;
extern std::vector<ItemTrackerItem> openChestItems;
extern std::vector<ItemTrackerItem> beanSoulItems;
extern std::vector<ItemTrackerItem> bossSoulItems;
extern std::vector<ItemTrackerItem> jabbernutItems;
extern std::vector<ItemTrackerItem> ocarinaButtonItems;
extern std::vector<ItemTrackerItem> overworldKeyItems;
extern std::vector<ItemTrackerItem> fishingPoleItems;
extern std::vector<ItemTrackerItem> dungeonItems;

extern std::map<uint16_t, std::string> itemTrackerDungeonShortNames;
extern std::map<uint16_t, std::string> itemTrackerBeanShortNames;
extern std::map<uint16_t, std::string> itemTrackerBossShortNames;
extern std::map<uint16_t, std::string> itemTrackerJabberNutShortNames;
extern std::map<uint16_t, std::string> itemTrackerOcarinaButtonShortNames;
extern std::map<uint16_t, std::string> itemTrackerOverworldKeyShortNames;
extern std::unordered_map<uint32_t, ItemTrackerItem> actualItemTrackerItemMap;
extern std::vector<uint32_t> buttonMap;

bool IsValidSaveFile();
ItemTrackerNumbers GetItemCurrentAndMax(ItemTrackerItem item);
void UpdateVectors();
