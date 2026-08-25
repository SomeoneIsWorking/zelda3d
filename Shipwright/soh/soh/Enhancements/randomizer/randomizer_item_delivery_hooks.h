#pragma once

#include <cstdint>
#include "soh/Enhancements/randomizer/randomizer.h"

typedef struct EnExItem EnExItem;
typedef struct ItemBHeart ItemBHeart;
typedef struct ItemEtcetera ItemEtcetera;

void RandomizerItemQueueReset();
bool RandomizerItemQueueIsDrained();
GetItemEntry RandomizerItemQueueCurrentEntry();
void RandomizerOnFlagSetHandler(int16_t flagType, int16_t flag);
void RandomizerOnSceneFlagSetHandler(int16_t sceneNum, int16_t flagType, int16_t flag);
void RandomizerOnPlayerUpdateForRCQueueHandler();
void RandomizerOnPlayerUpdateForItemQueueHandler();
void RandomizerOnItemReceiveHandler(GetItemEntry receivedItemEntry);
void EnItem00_DrawRandomizedItem(EnItem00* enItem00, PlayState* play);
void ItemBHeart_DrawRandomizedItem(ItemBHeart* itemBHeart, PlayState* play);
void ItemBHeart_UpdateRandomizedItem(Actor* actor, PlayState* play);
void EnExItem_WaitForObjectRandomized(EnExItem* enExItem, PlayState* play);
void ItemEtcetera_DrawRandomizedItem(ItemEtcetera* itemEtcetera, PlayState* play);
void ItemEtcetera_DrawRandomizedItemThroughLens(ItemEtcetera* itemEtcetera, PlayState* play);
void ItemEtcetera_func_80B858B4_Randomized(ItemEtcetera* itemEtcetera, PlayState* play);
void ItemEtcetera_UpdateRandomizedFireArrow(ItemEtcetera* itemEtcetera, PlayState* play);
uint8_t EnDs_RandoCanGetGrannyItem();
uint8_t EnJs_RandoCanGetCarpetMerchantItem();
uint8_t EnGm_RandoCanGetMedigoronItem();
void RandomizerSetChestGameRandomizerInf(RandomizerCheck rc);
void func_8083A434_override(PlayState* play, Player* player);
bool ShouldGiveFishingPrize(float fishLength);
