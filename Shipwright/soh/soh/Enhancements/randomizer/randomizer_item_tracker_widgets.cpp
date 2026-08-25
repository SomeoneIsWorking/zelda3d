#include "randomizer_item_tracker_widgets.h"

#include <string>

#include "randomizerTypes.h"
#include "randomizer_check_tracker.h"
#include "randomizer_item_tracker_model.h"
#include "soh/OTRGlobals.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/randomizer.h"

#include <fast/Fast3dGui.h>

extern "C" {
#include <z64.h>
#include "variables.h"
#include "macros.h"
#include "functions/game_state.h"
}

using namespace UIWidgets;

#define IM_COL_WHITE IM_COL32(255, 255, 255, 255)
#define IM_COL_RED IM_COL32(255, 0, 0, 255)
#define IM_COL_GREEN IM_COL32(0, 255, 0, 255)
#define IM_COL_GRAY IM_COL32(155, 155, 155, 255)
#define IM_COL_PURPLE IM_COL32(180, 90, 200, 255)
#define IM_COL_LIGHT_YELLOW IM_COL32(255, 255, 130, 255)

void DrawItemCount(ItemTrackerItem item, bool hideMax) {
    if (!GameInteractor::IsSaveLoaded()) {
        return;
    }
    int iconSize = CVarGetInteger(CVAR_TRACKER_ITEM("IconSize"), 36);
    int textSize = CVarGetInteger(CVAR_TRACKER_ITEM("TextSize"), 13);
    ItemTrackerNumbers currentAndMax = GetItemCurrentAndMax(item);
    ImVec2 p = ImGui::GetCursorScreenPos();
    int32_t trackerNumberDisplayMode =
        CVarGetInteger(CVAR_TRACKER_ITEM("ItemCountType"), ITEM_TRACKER_NUMBER_CURRENT_CAPACITY_ONLY);
    int32_t trackerKeyNumberDisplayMode = CVarGetInteger(CVAR_TRACKER_ITEM("KeyCounts"), KEYS_COLLECTED_MAX);
    float textScalingFactor = static_cast<float>(iconSize) / 36.0f;
    uint32_t actualItemId = INV_CONTENT(item.id);
    bool hasItem = actualItemId != ITEM_NONE;

    if (CVarGetInteger(CVAR_TRACKER_ITEM("HookshotIdentifier"), 0)) {
        if ((actualItemId == ITEM_HOOKSHOT || actualItemId == ITEM_LONGSHOT) && hasItem) {

            // Calculate the scaled position for the text
            ImVec2 textPos =
                ImVec2(p.x + (iconSize / 2) -
                           (ImGui::CalcTextSize(item.id == ITEM_HOOKSHOT ? "H" : "L").x * textScalingFactor / 2) +
                           8 * textScalingFactor,
                       p.y - 22 * textScalingFactor);

            ImGui::SetCursorScreenPos(textPos);
            ImGui::SetWindowFontScale(textScalingFactor);

            ImGui::Text(item.id == ITEM_HOOKSHOT ? "H" : "L");
            ImGui::SetWindowFontScale(1.0f); // Reset font scale to the original state
        }
    }

    ImGui::SetWindowFontScale(textSize / 13.0f);

    if (item.id == ITEM_KEY_SMALL && IsValidSaveFile()) {
        std::string currentString = "";
        std::string maxString = hideMax ? "???" : std::to_string(currentAndMax.maxCapacity);
        ImU32 currentColor = IM_COL_WHITE;
        ImU32 maxColor = IM_COL_GREEN;
        // "Collected / Max", "Current / Collected / Max", "Current / Max"
        if (trackerKeyNumberDisplayMode == KEYS_CURRENT_COLLECTED_MAX ||
            trackerKeyNumberDisplayMode == KEYS_CURRENT_MAX) {
            currentString += std::to_string(currentAndMax.currentAmmo);
            currentString += "/";
        }
        if (trackerKeyNumberDisplayMode == KEYS_COLLECTED_MAX ||
            trackerKeyNumberDisplayMode == KEYS_CURRENT_COLLECTED_MAX) {
            currentString += std::to_string(currentAndMax.currentCapacity);
            currentString += "/";
        }

        ImGui::SetCursorScreenPos(
            ImVec2(p.x + (iconSize / 2) - (ImGui::CalcTextSize((currentString + maxString).c_str()).x / 2), p.y - 14));
        ImGui::PushStyleColor(ImGuiCol_Text, currentColor);
        ImGui::Text("%s", currentString.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, maxColor);
        ImGui::Text("%s", maxString.c_str());
        ImGui::PopStyleColor();
    } else if (currentAndMax.currentCapacity > 0 && trackerNumberDisplayMode != ITEM_TRACKER_NUMBER_NONE &&
               IsValidSaveFile()) {
        std::string currentString = "";
        std::string maxString = "";
        ImU32 currentColor = IM_COL_WHITE;
        ImU32 maxColor = item.id == QUEST_SKULL_TOKEN ? IM_COL_RED : IM_COL_GREEN;

        bool shouldAlignToLeft = CVarGetInteger(CVAR_TRACKER_ITEM("ItemCountAlignLeft"), 0) &&
                                 trackerNumberDisplayMode != ITEM_TRACKER_NUMBER_CAPACITY &&
                                 trackerNumberDisplayMode != ITEM_TRACKER_NUMBER_AMMO;

        bool shouldDisplayAmmo = trackerNumberDisplayMode == ITEM_TRACKER_NUMBER_AMMO ||
                                 trackerNumberDisplayMode == ITEM_TRACKER_NUMBER_CURRENT_AMMO_ONLY ||
                                 // These items have a static capacity, so display ammo instead
                                 item.id == ITEM_BEAN || item.id == QUEST_SKULL_TOKEN ||
                                 item.id == ITEM_HEART_CONTAINER || item.id == ITEM_HEART_PIECE;

        bool shouldDisplayMax = !(trackerNumberDisplayMode == ITEM_TRACKER_NUMBER_CURRENT_CAPACITY_ONLY ||
                                  trackerNumberDisplayMode == ITEM_TRACKER_NUMBER_CURRENT_AMMO_ONLY);

        if (shouldDisplayAmmo) {
            currentString = std::to_string(currentAndMax.currentAmmo);
            if (currentAndMax.currentAmmo >= currentAndMax.currentCapacity) {
                if (item.id == QUEST_SKULL_TOKEN) {
                    currentColor = IM_COL_RED;
                } else {
                    currentColor = IM_COL_GREEN;
                }
            }
            if (shouldDisplayMax) {
                currentString += "/";
                maxString = std::to_string(currentAndMax.currentCapacity);
            }
            if (currentAndMax.currentAmmo <= 0) {
                currentColor = IM_COL_GRAY;
            }
        } else {
            currentString = std::to_string(currentAndMax.currentCapacity);
            if (currentAndMax.currentCapacity >= currentAndMax.maxCapacity) {
                currentColor = IM_COL_GREEN;
            } else if (shouldDisplayMax) {
                currentString += "/";
                maxString = std::to_string(currentAndMax.maxCapacity);
            }
        }

        float x = shouldAlignToLeft
                      ? p.x
                      : p.x + (iconSize / 2) - (ImGui::CalcTextSize((currentString + maxString).c_str()).x / 2);

        ImGui::SetCursorScreenPos(ImVec2(x, p.y - 14));
        ImGui::PushStyleColor(ImGuiCol_Text, currentColor);
        ImGui::Text("%s", currentString.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, maxColor);
        ImGui::Text("%s", maxString.c_str());
        ImGui::PopStyleColor();
    } else if (item.id == RG_TRIFORCE_PIECE && IS_RANDO &&
               (OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_TRIFORCE_HUNT) != RO_TRIFORCE_HUNT_OFF) &&
               IsValidSaveFile()) {
        std::string currentString = "";
        std::string requiredString = "";
        std::string maxString = "";
        uint8_t piecesRequired =
            (OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_TRIFORCE_HUNT_PIECES_REQUIRED) + 1);
        uint8_t piecesTotal =
            (OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_TRIFORCE_HUNT_PIECES_TOTAL) + 1);
        ImU32 currentColor = gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected >= piecesRequired
                                 ? IM_COL_GREEN
                                 : IM_COL_WHITE;
        ImU32 maxColor = IM_COL_GREEN;
        int32_t trackerTriforcePieceNumberDisplayMode =
            CVarGetInteger(CVAR_TRACKER_ITEM("TriforcePieceCounts"), TRIFORCE_PIECE_COLLECTED_REQUIRED_MAX);

        currentString += std::to_string(gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected);
        currentString += "/";
        // gItemTrackerTriforcePieceTrack
        if (trackerTriforcePieceNumberDisplayMode == TRIFORCE_PIECE_COLLECTED_REQUIRED_MAX) {
            currentString += std::to_string(piecesRequired);
            currentString += "/";
            maxString += std::to_string(piecesTotal);
        } else if (trackerTriforcePieceNumberDisplayMode == TRIFORCE_PIECE_COLLECTED_REQUIRED) {
            maxString += std::to_string(piecesRequired);
        }

        ImGui::SetCursorScreenPos(
            ImVec2(p.x + (iconSize / 2) - (ImGui::CalcTextSize((currentString + maxString).c_str()).x / 2), p.y - 14));
        ImGui::PushStyleColor(ImGuiCol_Text, currentColor);
        ImGui::Text("%s", currentString.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, maxColor);
        ImGui::Text("%s", maxString.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y - 14));
        ImGui::Text("");
    }
}

void DrawEquip(ItemTrackerItem item) {
    bool hasEquip = HasEquipment(item);
    float iconSize = static_cast<float>(CVarGetInteger(CVAR_TRACKER_ITEM("IconSize"), 36));
    ImGui::Image(std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                     ->GetTextureByName(hasEquip && IsValidSaveFile() ? item.name : item.nameFaded),
                 ImVec2(iconSize, iconSize), ImVec2(0.0f, 0.0f), ImVec2(1, 1));

    Tooltip(SohUtils::GetItemName(item.id).c_str());
}

void DrawQuest(ItemTrackerItem item) {
    bool hasQuestItem = HasQuestItem(item);
    float iconSize = static_cast<float>(CVarGetInteger(CVAR_TRACKER_ITEM("IconSize"), 36));
    ImGui::BeginGroup();
    ImGui::ImageWithBg(
        std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
            ->GetTextureByName(hasQuestItem && IsValidSaveFile() ? item.name : item.nameFaded),
        ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1));

    if (item.id == QUEST_SKULL_TOKEN) {
        DrawItemCount(item, false);
    }

    ImGui::EndGroup();

    Tooltip(SohUtils::GetQuestItemName(item.id).c_str());
};

bool HasBossSoul(RandomizerInf bossSoul) {
    uint8_t soulSetting = OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_SHUFFLE_BOSS_SOULS);
    bool isSoulRandomized = IS_RANDO && (soulSetting == RO_BOSS_SOULS_ON_PLUS_GANON ||
                                         (soulSetting == RO_BOSS_SOULS_ON && bossSoul != RAND_INF_GANON_SOUL));

    return isSoulRandomized ? Flags_GetRandomizerInf(bossSoul) : true;
}

void DrawItem(ItemTrackerItem item) {

    uint32_t actualItemId = GameInteractor::IsSaveLoaded() ? INV_CONTENT(item.id) : ITEM_NONE;
    float iconSize = static_cast<float>(CVarGetInteger(CVAR_TRACKER_ITEM("IconSize"), 36));
    bool hasItem = actualItemId != ITEM_NONE;
    std::string itemName = "";

    // Hack fix as RG_MARKET_SHOOTING_GALLERY_KEY is RandomizerGet #255 which collides
    // with ITEM_NONE (ItemId #255) due to the lack of a modid to separate them
    if (item.name != "ITEM_KEY_SMALL" && item.id == ITEM_NONE) {
        return;
    }

    switch (item.id) {
        case ITEM_HEART_CONTAINER:
            actualItemId = item.id;
            hasItem = gSaveContext.ship.stats.heartContainers > 0;
            break;
        case ITEM_HEART_PIECE:
            actualItemId = item.id;
            hasItem = gSaveContext.ship.stats.heartPieces > 0;
            break;
        case ITEM_MAGIC_SMALL:
        case ITEM_MAGIC_LARGE:
            actualItemId = gSaveContext.magicLevel == 2 ? ITEM_MAGIC_LARGE : ITEM_MAGIC_SMALL;
            hasItem = gSaveContext.magicLevel > 0;
            break;
        case ITEM_WALLET_ADULT:
        case ITEM_WALLET_GIANT:
            actualItemId = CUR_UPG_VALUE(UPG_WALLET) == 2 ? ITEM_WALLET_GIANT : ITEM_WALLET_ADULT;
            hasItem = !IS_RANDO || Flags_GetRandomizerInf(RAND_INF_HAS_WALLET);
            break;
        case ITEM_BRACELET:
        case ITEM_GAUNTLETS_SILVER:
        case ITEM_GAUNTLETS_GOLD:
            actualItemId = CUR_UPG_VALUE(UPG_STRENGTH) >= 3   ? ITEM_GAUNTLETS_GOLD
                           : CUR_UPG_VALUE(UPG_STRENGTH) == 2 ? ITEM_GAUNTLETS_SILVER
                                                              : ITEM_BRACELET;
            hasItem = CUR_UPG_VALUE(UPG_STRENGTH) > 0;
            break;
        case ITEM_SCALE_SILVER:
        case ITEM_SCALE_GOLDEN:
            actualItemId = CUR_UPG_VALUE(UPG_SCALE) == 2 ? ITEM_SCALE_GOLDEN : ITEM_SCALE_SILVER;
            hasItem = CUR_UPG_VALUE(UPG_SCALE) > 0;
            break;
        case ITEM_RUPEE_GREEN:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_GREG_FOUND);
            break;
        case RG_TRIFORCE_PIECE:
            actualItemId = item.id;
            hasItem = IS_RANDO && (OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_TRIFORCE_HUNT) !=
                                   RO_TRIFORCE_HUNT_OFF);
            itemName = "Triforce Piece";
            break;
        case ITEM_NAYRUS_LOVE:
            if (IS_RANDO && OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_ROCS_FEATHER)) {
                hasItem = Flags_GetRandomizerInf(RAND_INF_OBTAINED_NAYRUS_LOVE);
            }
            break;
        case RG_ROCS_FEATHER:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_OBTAINED_ROCS_FEATHER);
            itemName = "Roc's Feather";
            break;
        case RG_DEATH_MOUNTAIN_CRATER_BEAN_SOUL:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_DEATH_MOUNTAIN_CRATER_BEAN_SOUL);
            itemName = "Death Mountain Crater Bean Soul";
            break;
        case RG_DEATH_MOUNTAIN_TRAIL_BEAN_SOUL:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_DEATH_MOUNTAIN_TRAIL_BEAN_SOUL);
            itemName = "Death Mountain Trail Bean Soul";
            break;
        case RG_DESERT_COLOSSUS_BEAN_SOUL:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_DESERT_COLOSSUS_BEAN_SOUL);
            itemName = "Desert Colossus Bean Soul";
            break;
        case RG_GERUDO_VALLEY_BEAN_SOUL:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_GERUDO_VALLEY_BEAN_SOUL);
            itemName = "Gerudo Valley Bean Soul";
            break;
        case RG_GRAVEYARD_BEAN_SOUL:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_GRAVEYARD_BEAN_SOUL);
            itemName = "Graveyard Bean Soul";
            break;
        case RG_KOKIRI_FOREST_BEAN_SOUL:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_KOKIRI_FOREST_BEAN_SOUL);
            itemName = "Kokiri Forest Bean Soul";
            break;
        case RG_LAKE_HYLIA_BEAN_SOUL:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_LAKE_HYLIA_BEAN_SOUL);
            itemName = "Lake Hylia Bean Soul";
            break;
        case RG_LOST_WOODS_BRIDGE_BEAN_SOUL:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_LOST_WOODS_BRIDGE_BEAN_SOUL);
            itemName = "Lost Woods Bridge Bean Soul";
            break;
        case RG_LOST_WOODS_BEAN_SOUL:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_LOST_WOODS_BEAN_SOUL);
            itemName = "Lost Woods Theatre Bean Soul";
            break;
        case RG_ZORAS_RIVER_BEAN_SOUL:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_ZORAS_RIVER_BEAN_SOUL);
            itemName = "Zora's River Bean Soul";
            break;
        case RG_GOHMA_SOUL:
            actualItemId = item.id;
            hasItem = HasBossSoul(RAND_INF_GOHMA_SOUL);
            itemName = "Gohma's Soul";
            break;
        case RG_KING_DODONGO_SOUL:
            actualItemId = item.id;
            hasItem = HasBossSoul(RAND_INF_KING_DODONGO_SOUL);
            itemName = "King Dodongo's Soul";
            break;
        case RG_BARINADE_SOUL:
            actualItemId = item.id;
            hasItem = HasBossSoul(RAND_INF_BARINADE_SOUL);
            itemName = "Barinade's Soul";
            break;
        case RG_PHANTOM_GANON_SOUL:
            actualItemId = item.id;
            hasItem = HasBossSoul(RAND_INF_PHANTOM_GANON_SOUL);
            itemName = "Phantom Ganon's Soul";
            break;
        case RG_VOLVAGIA_SOUL:
            actualItemId = item.id;
            hasItem = HasBossSoul(RAND_INF_VOLVAGIA_SOUL);
            itemName = "Volvagia's Soul";
            break;
        case RG_MORPHA_SOUL:
            actualItemId = item.id;
            hasItem = HasBossSoul(RAND_INF_MORPHA_SOUL);
            itemName = "Morpha's Soul";
            break;
        case RG_BONGO_BONGO_SOUL:
            actualItemId = item.id;
            hasItem = HasBossSoul(RAND_INF_BONGO_BONGO_SOUL);
            itemName = "Bongo Bongo's Soul";
            break;
        case RG_TWINROVA_SOUL:
            actualItemId = item.id;
            hasItem = HasBossSoul(RAND_INF_TWINROVA_SOUL);
            itemName = "Twinrova's Soul";
            break;
        case RG_GANON_SOUL:
            actualItemId = item.id;
            hasItem = HasBossSoul(RAND_INF_GANON_SOUL);
            itemName = "Ganon's Soul";
            break;

        case RG_SPEAK_DEKU:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_CAN_SPEAK_DEKU);
            itemName = "Deku Jabber Nut";
            break;
        case RG_SPEAK_GERUDO:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_CAN_SPEAK_GERUDO);
            itemName = "Gerudo Jabber Nut";
            break;
        case RG_SPEAK_GORON:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_CAN_SPEAK_GORON);
            itemName = "Goron Jabber Nut";
            break;
        case RG_SPEAK_HYLIAN:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_CAN_SPEAK_HYLIAN);
            itemName = "Hylian Jabber Nut";
            break;
        case RG_SPEAK_KOKIRI:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_CAN_SPEAK_KOKIRI);
            itemName = "Kokiri Jabber Nut";
            break;
        case RG_SPEAK_ZORA:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_CAN_SPEAK_ZORA);
            itemName = "Zora Jabber Nut";
            break;

        case RG_OCARINA_A_BUTTON:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_HAS_OCARINA_A);
            itemName = "Ocarina A Button";
            break;
        case RG_OCARINA_C_UP_BUTTON:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_HAS_OCARINA_C_UP);
            itemName = "Ocarina C Up Button";
            break;
        case RG_OCARINA_C_DOWN_BUTTON:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_HAS_OCARINA_C_DOWN);
            itemName = "Ocarina C Down Button";
            break;
        case RG_OCARINA_C_LEFT_BUTTON:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_HAS_OCARINA_C_LEFT);
            itemName = "Ocarina C Left Button";
            break;
        case RG_OCARINA_C_RIGHT_BUTTON:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_HAS_OCARINA_C_RIGHT);
            itemName = "Ocarina C Right Button";
            break;
        case ITEM_FISHING_POLE:
            actualItemId = item.id;
            hasItem = IS_RANDO && Flags_GetRandomizerInf(RAND_INF_FISHING_POLE_FOUND);
            itemName = "Fishing Pole";
            break;

        case RG_GUARD_HOUSE_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_GUARD_HOUSE_KEY_OBTAINED);
            itemName = "Guard House Key";
            break;
        case RG_MARKET_BAZAAR_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_MARKET_BAZAAR_KEY_OBTAINED);
            itemName = "Market Bazaar Key";
            break;
        case RG_MARKET_POTION_SHOP_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_MARKET_POTION_SHOP_KEY_OBTAINED);
            itemName = "Market Potion Shop Key";
            break;
        case RG_MASK_SHOP_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_MASK_SHOP_KEY_OBTAINED);
            itemName = "Mask Shop Key";
            break;
        case RG_MARKET_SHOOTING_GALLERY_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_MARKET_SHOOTING_GALLERY_KEY_OBTAINED);
            itemName = "Market Shooting Gallery Key";
            break;
        case RG_BOMBCHU_BOWLING_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_BOMBCHU_BOWLING_KEY_OBTAINED);
            itemName = "Bombchu Bowling Key";
            break;
        case RG_TREASURE_CHEST_GAME_BUILDING_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_TREASURE_CHEST_GAME_BUILDING_KEY_OBTAINED);
            itemName = "Treasure Chest Game Building Key";
            break;
        case RG_BOMBCHU_SHOP_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_BOMBCHU_SHOP_KEY_OBTAINED);
            itemName = "Bombchu Shop Key";
            break;
        case RG_RICHARDS_HOUSE_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_RICHARDS_HOUSE_KEY_OBTAINED);
            itemName = "Richards House Key";
            break;
        case RG_ALLEY_HOUSE_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_ALLEY_HOUSE_KEY_OBTAINED);
            itemName = "Alley House Key";
            break;
        case RG_KAK_BAZAAR_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_KAK_BAZAAR_KEY_OBTAINED);
            itemName = "Kak Bazaar Key";
            break;
        case RG_KAK_POTION_SHOP_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_KAK_POTION_SHOP_KEY_OBTAINED);
            itemName = "Kak Potion Shop Key";
            break;
        case RG_BOSS_HOUSE_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_BOSS_HOUSE_KEY_OBTAINED);
            itemName = "Boss House Key";
            break;
        case RG_GRANNYS_POTION_SHOP_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_GRANNYS_POTION_SHOP_KEY_OBTAINED);
            itemName = "Granny's Potion Shop Key";
            break;
        case RG_SKULLTULA_HOUSE_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_SKULLTULA_HOUSE_KEY_OBTAINED);
            itemName = "Skulltula House Key";
            break;
        case RG_IMPAS_HOUSE_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_IMPAS_HOUSE_KEY_OBTAINED);
            itemName = "Impa's House Key";
            break;
        case RG_WINDMILL_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_WINDMILL_KEY_OBTAINED);
            itemName = "Windmill Key";
            break;
        case RG_KAK_SHOOTING_GALLERY_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_KAK_SHOOTING_GALLERY_KEY_OBTAINED);
            itemName = "Kak Shooting Gallery Key";
            break;
        case RG_DAMPES_HUT_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_DAMPES_HUT_KEY_OBTAINED);
            itemName = "Dampé's Hut Key";
            break;
        case RG_TALONS_HOUSE_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_TALONS_HOUSE_KEY_OBTAINED);
            itemName = "Talon's House Key";
            break;
        case RG_STABLES_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_STABLES_KEY_OBTAINED);
            itemName = "Stables Key";
            break;
        case RG_BACK_TOWER_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_BACK_TOWER_KEY_OBTAINED);
            itemName = "Back Tower Key";
            break;
        case RG_HYLIA_LAB_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_HYLIA_LAB_KEY_OBTAINED);
            itemName = "Hylia Lab Key";
            break;
        case RG_FISHING_HOLE_KEY:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_FISHING_HOLE_KEY_OBTAINED);
            itemName = "Fishing Hole Key";
            break;
        case RG_BRONZE_SCALE:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_CAN_SWIM);
            itemName = "Swim";
            break;
        case RG_CRAWL:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_CAN_CRAWL);
            itemName = "Crawl";
            break;
        case RG_CLIMB:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_CAN_CLIMB);
            itemName = "Climb";
            break;
        case RG_POWER_BRACELET:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_CAN_GRAB);
            itemName = "Grab";
            break;
        case RG_OPEN_CHEST:
            actualItemId = item.id;
            hasItem = Flags_GetRandomizerInf(RAND_INF_CAN_OPEN_CHEST);
            itemName = "Open";
            break;
    }

    if (GameInteractor::IsSaveLoaded() &&
        (hasItem && item.id != actualItemId &&
         actualItemTrackerItemMap.find(actualItemId) != actualItemTrackerItemMap.end())) {
        item = actualItemTrackerItemMap[actualItemId];
    }

    ImGui::BeginGroup();

    ImGui::Image(std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                     ->GetTextureByName(hasItem && IsValidSaveFile() ? item.name : item.nameFaded),
                 ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1));

    DrawItemCount(item, false);

    if (item.id >= RG_DEATH_MOUNTAIN_CRATER_BEAN_SOUL && item.id <= RG_ZORAS_RIVER_BEAN_SOUL) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        std::string beanName = itemTrackerBeanShortNames[item.id];
        ImGui::SetCursorScreenPos(
            ImVec2(p.x + (iconSize / 2) - (ImGui::CalcTextSize(beanName.c_str()).x / 2), p.y - (iconSize + 13)));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL_WHITE);
        ImGui::Text("%s", beanName.c_str());
        ImGui::PopStyleColor();
    }

    if (item.id >= RG_GOHMA_SOUL && item.id <= RG_GANON_SOUL) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        std::string bossName = itemTrackerBossShortNames[item.id];
        ImGui::SetCursorScreenPos(
            ImVec2(p.x + (iconSize / 2) - (ImGui::CalcTextSize(bossName.c_str()).x / 2), p.y - (iconSize + 13)));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL_WHITE);
        ImGui::Text("%s", bossName.c_str());
        ImGui::PopStyleColor();
    }

    if (item.id >= RG_SPEAK_DEKU && item.id <= RG_SPEAK_ZORA) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        std::string name = itemTrackerJabberNutShortNames[item.id];
        ImGui::SetCursorScreenPos(
            ImVec2(p.x + (iconSize / 2) - (ImGui::CalcTextSize(name.c_str()).x / 2), p.y - (iconSize + 13)));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL_WHITE);
        ImGui::Text("%s", name.c_str());
        ImGui::PopStyleColor();
    }

    if (item.id >= RG_OCARINA_A_BUTTON && item.id <= RG_OCARINA_C_RIGHT_BUTTON) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        std::string ocarinaButtonName = itemTrackerOcarinaButtonShortNames[item.id];
        ImGui::SetCursorScreenPos(ImVec2(p.x + (iconSize / 2) - (ImGui::CalcTextSize(ocarinaButtonName.c_str()).x / 2),
                                         p.y - (iconSize + 13)));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL_WHITE);
        ImGui::Text("%s", ocarinaButtonName.c_str());
        ImGui::PopStyleColor();
    }

    if (item.id >= RG_GUARD_HOUSE_KEY && item.id <= RG_FISHING_HOLE_KEY) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        std::string overworldKeyName = itemTrackerOverworldKeyShortNames[item.id];
        ImGui::SetCursorScreenPos(ImVec2(p.x + (iconSize / 2) - (ImGui::CalcTextSize(overworldKeyName.c_str()).x / 2),
                                         p.y - (iconSize + 13)));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL_WHITE);
        ImGui::Text("%s", overworldKeyName.c_str());
        ImGui::PopStyleColor();
    }

    if (item.id >= RG_BRONZE_SCALE && item.id <= RG_OPEN_CHEST) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(
            ImVec2(p.x + (iconSize / 2) - (ImGui::CalcTextSize(itemName.c_str()).x / 2), p.y - (iconSize + 2)));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL_WHITE);
        ImGui::Text("%s", itemName.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndGroup();

    if (itemName == "") {
        itemName = SohUtils::GetItemName(item.id);
    }

    Tooltip(itemName.c_str());
}

void DrawBottle(ItemTrackerItem item) {
    uint32_t actualItemId =
        GameInteractor::IsSaveLoaded() ? (gSaveContext.inventory.items[SLOT(item.id) + item.data]) : false;
    bool hasItem = actualItemId != ITEM_NONE;

    if (GameInteractor::IsSaveLoaded() &&
        (hasItem && item.id != actualItemId &&
         actualItemTrackerItemMap.find(actualItemId) != actualItemTrackerItemMap.end())) {
        item = actualItemTrackerItemMap[actualItemId];
    }

    float iconSize = static_cast<float>(CVarGetInteger(CVAR_TRACKER_ITEM("IconSize"), 36));
    ImGui::Image(std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                     ->GetTextureByName(hasItem && IsValidSaveFile() ? item.name : item.nameFaded),
                 ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1));

    Tooltip(SohUtils::GetItemName(item.id).c_str());
};

void DrawDungeonItem(ItemTrackerItem item) {
    uint32_t itemId = item.id;
    ImU32 dungeonColor = IM_COL_WHITE;
    uint32_t bitMask = 1 << (item.id - ITEM_KEY_BOSS); // Bitset starts at ITEM_KEY_BOSS == 0. the rest are sequential
    float iconSize = static_cast<float>(CVarGetInteger(CVAR_TRACKER_ITEM("IconSize"), 36));
    bool hasItem = GameInteractor::IsSaveLoaded() ? (bitMask & gSaveContext.inventory.dungeonItems[item.data]) : false;
    bool hasSmallKey = GameInteractor::IsSaveLoaded() ? ((gSaveContext.inventory.dungeonKeys[item.data]) >= 0) : false;
    ImGui::BeginGroup();
    if (itemId == ITEM_KEY_SMALL) {
        ImGui::Image(std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                         ->GetTextureByName(hasSmallKey && IsValidSaveFile() ? item.name : item.nameFaded),
                     ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1));
    } else {
        ImGui::Image(std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                         ->GetTextureByName(hasItem && IsValidSaveFile() ? item.name : item.nameFaded),
                     ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1));
    }

    if (CheckTracker::IsAreaSpoiled(RandomizerCheckObjects::GetRCAreaBySceneID(static_cast<SceneID>(item.data))) &&
        GameInteractor::IsSaveLoaded()) {
        dungeonColor = (ResourceMgr_IsSceneMasterQuest(item.data) ? IM_COL_PURPLE : IM_COL_LIGHT_YELLOW);
    }

    if (itemId == ITEM_KEY_SMALL) {
        DrawItemCount(item, !CheckTracker::IsAreaSpoiled(
                                RandomizerCheckObjects::GetRCAreaBySceneID(static_cast<SceneID>(item.data))));

        ImVec2 p = ImGui::GetCursorScreenPos();
        // offset puts the text at the correct level. for some reason, if the save is loaded, the margin is 3 pixels
        // higher only for small keys, so we use 16 then. Otherwise, 13 is where everything else is
        int offset = GameInteractor::IsSaveLoaded() ? 16 : 13;
        std::string dungeonName = itemTrackerDungeonShortNames[item.data];
        ImGui::SetCursorScreenPos(
            ImVec2(p.x + (iconSize / 2) - (ImGui::CalcTextSize(dungeonName.c_str()).x / 2), p.y - (iconSize + offset)));
        ImGui::PushStyleColor(ImGuiCol_Text, dungeonColor);
        ImGui::Text("%s", dungeonName.c_str());
        ImGui::PopStyleColor();
    }

    if (itemId == ITEM_DUNGEON_MAP && (item.data == SCENE_DEKU_TREE || item.data == SCENE_DODONGOS_CAVERN ||
                                       item.data == SCENE_JABU_JABU || item.data == SCENE_ICE_CAVERN)) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        std::string dungeonName = itemTrackerDungeonShortNames[item.data];
        ImGui::SetCursorScreenPos(
            ImVec2(p.x + (iconSize / 2) - (ImGui::CalcTextSize(dungeonName.c_str()).x / 2), p.y - (iconSize + 13)));
        ImGui::PushStyleColor(ImGuiCol_Text, dungeonColor);
        ImGui::Text("%s", dungeonName.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();

    Tooltip(SohUtils::GetItemName(item.id).c_str());
}

void DrawSong(ItemTrackerItem item) {
    float iconSize = static_cast<float>(CVarGetInteger(CVAR_TRACKER_ITEM("IconSize"), 36));
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool hasSong = HasSong(item);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 6, p.y));
    ImGui::Image(std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                     ->GetTextureByName(hasSong && IsValidSaveFile() ? item.name : item.nameFaded),
                 ImVec2(iconSize / 1.5f, iconSize), ImVec2(0, 0), ImVec2(1, 1));
    Tooltip(SohUtils::GetQuestItemName(item.id).c_str());
}

void DrawTotalChecks() {
    uint16_t totalChecks = CheckTracker::GetTotalChecks();
    uint16_t totalChecksGotten = CheckTracker::GetTotalChecksGotten();

    ImGui::BeginGroup();
    if (CVarGetInteger(CVAR_TRACKER_ITEM("WindowType"), TRACKER_WINDOW_FLOATING) == TRACKER_WINDOW_FLOATING) {
        ImGui::SetWindowFontScale(2.5);
    } else {
        ImGui::SetWindowFontScale(1);
    }
    ImGui::Text("Checks: %d/%d", totalChecksGotten, totalChecks);
    ImGui::EndGroup();
}
