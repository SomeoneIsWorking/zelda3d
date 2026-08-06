#pragma once

#define BTN_CUSTOM_MODIFIER1 0x0040
#define BTN_CUSTOM_MODIFIER2 0x0080

#define BTN_CUSTOM_OCARINA_NOTE_D4 ((CONTROLLERBUTTONS_T)0x00010000)
#define BTN_CUSTOM_OCARINA_NOTE_F4 ((CONTROLLERBUTTONS_T)0x00020000)
#define BTN_CUSTOM_OCARINA_NOTE_A4 ((CONTROLLERBUTTONS_T)0x00040000)
#define BTN_CUSTOM_OCARINA_NOTE_B4 ((CONTROLLERBUTTONS_T)0x00080000)
#define BTN_CUSTOM_OCARINA_NOTE_D5 ((CONTROLLERBUTTONS_T)0x00100000)
#define BTN_CUSTOM_OCARINA_DISABLE_SONGS ((CONTROLLERBUTTONS_T)0x00200000)
#define BTN_CUSTOM_OCARINA_PITCH_UP ((CONTROLLERBUTTONS_T)0x00400000)
#define BTN_CUSTOM_OCARINA_PITCH_DOWN ((CONTROLLERBUTTONS_T)0x00800000)

#define M_PIf 3.14159265358979323846f
#define M_PI_2f 1.57079632679489661923f // pi/2

#ifdef __cplusplus
#include <stdint.h>
#include <memory>
#include <string>
#include <unordered_map>

struct ExtensionEntry {
    std::string path;
    std::string ext;
};

extern std::unordered_map<std::string, ExtensionEntry> ExtensionCache;

const std::string appShortName = "soh";

class Randomizer;
class SaveStateMgr;

namespace Rando {
class Context;
}

namespace Ship {
class Context;
}

struct ImFont;

class OTRGlobals {
  public:
    static OTRGlobals* Instance;

    Ship::Context* context;
    std::shared_ptr<SaveStateMgr> gSaveStateMgr;
    std::shared_ptr<Randomizer> gRandomizer;
    std::shared_ptr<Rando::Context> gRandoContext;

    ImFont* fontMonoSmall = nullptr;
    ImFont* fontStandard = nullptr;
    ImFont* fontStandardLarger = nullptr;
    ImFont* fontStandardLargest = nullptr;
    ImFont* fontMono = nullptr;
    ImFont* fontMonoLarger = nullptr;
    ImFont* fontMonoLargest = nullptr;
    ImFont* fontJapanese = nullptr;

    OTRGlobals();
    ~OTRGlobals();

    void ScaleImGui();
    void Initialize();
    void RunExtract(int argc, char* argv[]);
    bool HasMasterQuest();
    bool HasOriginal();
    uint32_t GetInterpolationFPS();

  private:
    bool hasMasterQuest;
    bool hasOriginal;
    ImFont* CreateFontWithSize(float size, std::string fontPath, bool isJapaneseFont = false);
};
#endif

#ifndef __cplusplus
/* The port ABI both games implement. One declaration site, so the compiler catches a
 * game whose definition drifts from it. Included HERE, inside the C-only guard, to keep
 * these exactly as C-visible as they were. */
#include "port/zelda3d_port_api.h"
void OTRAudio_Exit(void); // idempotent — safe to call before DeinitOTR


size_t GetEquipNowMessage(char* buffer, char* src, const size_t maxBufferSize);
u32 SpoilerFileExists(const char* spoilerFileName);
Sprite* GetSeedTexture(uint8_t index);
uint8_t GetSeedIconIndex(uint8_t index);
u8 Randomizer_GetSettingValue(RandomizerSettingKey randoSettingKey);
RandomizerCheck Randomizer_GetCheckFromActor(s16 actorId, s16 sceneNum, s16 actorParams);
ShopItemIdentity Randomizer_IdentifyShopItem(s32 sceneNum, u8 slotIndex);
void Randomizer_ParseSpoiler(const char* fileLoc);
GetItemEntry Randomizer_GetItemFromKnownCheck(RandomizerCheck randomizerCheck, GetItemID ogId);
GetItemEntry Randomizer_GetItemFromKnownCheckWithoutObtainabilityCheck(RandomizerCheck randomizerCheck, GetItemID ogId);
bool Randomizer_IsCheckShuffled(RandomizerCheck check);
GetItemEntry GetItemMystery();
ItemObtainability Randomizer_GetItemObtainabilityFromRandomizerCheck(RandomizerCheck randomizerCheck);
uint8_t Randomizer_IsSeedGenerated();
uint8_t Randomizer_IsSpoilerLoaded();
void Randomizer_SetSpoilerLoaded(bool spoilerLoaded);
uint8_t Randomizer_GenerateRandomizer();
void Randomizer_ShowRandomizerMenu();
GetItemEntry ItemTable_Retrieve(int16_t getItemID);
GetItemEntry ItemTable_RetrieveEntry(s16 modIndex, s16 getItemID);
void EntranceTracker_SetCurrentGrottoID(s16 entranceIndex);
void EntranceTracker_SetLastEntranceOverride(s16 entranceIndex);
void SaveManager_ThreadPoolWait();

GetItemID RetrieveGetItemIDFromItemID(ItemID itemID);
RandomizerGet RetrieveRandomizerGetFromItemID(ItemID itemID);

#endif

#ifdef __cplusplus
extern "C" {
#endif
uint64_t GetUnixTimestamp();
#ifdef __cplusplus
};
#endif