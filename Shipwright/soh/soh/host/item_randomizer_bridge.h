#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "soh/Enhancements/item-tables/ItemTableTypes.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "z64item.h"

#ifdef __cplusplus
extern "C" {
#endif

void VanillaItemTable_Init(void);

GetItemID RetrieveGetItemIDFromItemID(ItemID itemID);
RandomizerGet RetrieveRandomizerGetFromItemID(ItemID itemID);

Sprite* GetSeedTexture(uint8_t index);
uint8_t GetSeedIconIndex(uint8_t index);
size_t GetEquipNowMessage(char* buffer, char* src, size_t maxBufferSize);

void Randomizer_ParseSpoiler(const char* fileLoc);
uint32_t SpoilerFileExists(const char* spoilerFileName);
uint8_t Randomizer_GetSettingValue(RandomizerSettingKey randoSettingKey);
RandomizerCheck Randomizer_GetCheckFromActor(int16_t actorId, int16_t sceneNum, int16_t actorParams);
ShopItemIdentity Randomizer_IdentifyShopItem(int32_t sceneNum, uint8_t slotIndex);
GetItemEntry Randomizer_GetItemFromKnownCheck(RandomizerCheck randomizerCheck, GetItemID originalId);
GetItemEntry Randomizer_GetItemFromKnownCheckWithoutObtainabilityCheck(RandomizerCheck randomizerCheck,
                                                                       GetItemID originalId);
ItemObtainability Randomizer_GetItemObtainabilityFromRandomizerCheck(RandomizerCheck randomizerCheck);
bool Randomizer_IsCheckShuffled(RandomizerCheck check);
GetItemEntry GetItemMystery(void);
uint8_t Randomizer_IsSeedGenerated(void);
uint8_t Randomizer_IsSpoilerLoaded(void);
void Randomizer_SetSpoilerLoaded(bool spoilerLoaded);
uint8_t Randomizer_GenerateRandomizer(void);
void Randomizer_ShowRandomizerMenu(void);

GetItemEntry ItemTable_Retrieve(int16_t getItemID);
GetItemEntry ItemTable_RetrieveEntry(int16_t tableID, int16_t getItemID);

void EntranceTracker_SetCurrentGrottoID(int16_t entranceIndex);
void EntranceTracker_SetLastEntranceOverride(int16_t entranceIndex);

#ifdef __cplusplus
}
#endif
