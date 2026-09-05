#include "soh/OTRGlobals.h"
#include "debugSaveEditor.h"
#include "debugSaveEditorInternal.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/util.h"
#include "soh/SohGui/ImGuiUtils.h"

#include "soh/SohGui/UIWidgets.hpp"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SaveManager.h"

#include <spdlog/fmt/fmt.h>
#include <array>
#include <bit>
#include <map>
#include <string>
#include <libultraship/bridge.h>
#include <libultraship/libultraship.h>
#include <soh_assets.h>

#include <fast/Fast3dGui.h>

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions/game_state.h"
#include "macros.h"
#include "soh/cvar_prefixes.h"
extern PlayState* gPlayState;
}

#include "message_data_static.h"
extern "C" MessageTableEntry* sGerMessageEntryTablePtr;
extern "C" MessageTableEntry* sFraMessageEntryTablePtr;
extern "C" MessageTableEntry* sJpnMessageEntryTablePtr;

// Maps entries in the GS flag array to the area name it represents
std::vector<const char*> gsMapping = {
    "Deku Tree",
    "Dodongo's Cavern",
    "Inside Jabu-Jabu's Belly",
    "Forest Temple",
    "Fire Temple",
    "Water Temple",
    "Spirit Temple",
    "Shadow Temple",
    "Bottom of the Well",
    "Ice Cavern",
    "Hyrule Field",
    "Lon Lon Ranch",
    "Kokiri Forest",
    "Lost Woods, Sacred Forest Meadow",
    "Castle Town and Ganon's Castle",
    "Death Mountain Trail, Goron City",
    "Kakariko Village",
    "Zora Fountain, River",
    "Lake Hylia",
    "Gerudo Valley",
    "Gerudo Fortress",
    "Desert Colossus, Haunted Wasteland",
};

extern "C" u8 gAreaGsFlags[];

extern "C" u8 gAmmoItems[];

using namespace UIWidgets;

IntSliderOptions intSliderOptionsBase;
ButtonOptions buttonOptionsBase;
CheckboxOptions checkboxOptionsBase;
ComboboxOptions comboboxOptionsBase;
static std::map<std::string, ImGuiTextFilter> flagTableFilters;

// Modification of gAmmoItems that replaces ITEM_NONE with the item in inventory slot it represents
u8 gAllAmmoItems[] = {
    ITEM_STICK,     ITEM_NUT,          ITEM_BOMB,    ITEM_BOW,      ITEM_ARROW_FIRE, ITEM_DINS_FIRE,
    ITEM_SLINGSHOT, ITEM_OCARINA_TIME, ITEM_BOMBCHU, ITEM_LONGSHOT, ITEM_ARROW_ICE,  ITEM_FARORES_WIND,
    ITEM_BOOMERANG, ITEM_LENS,         ITEM_BEAN,    ITEM_HAMMER,
};

void DrawFlagTableArray16(const FlagTable& flagTable, uint16_t row, uint16_t& flags) {
    ImGui::PushID((std::to_string(row) + flagTable.name).c_str());
    for (int32_t flagIndex = 15; flagIndex >= 0; flagIndex--) {
        ImGui::SameLine();
        ImGui::PushID(flagIndex);
        bool hasDescription = !!flagTable.flagDescriptions.contains(row * 16 + flagIndex);
        uint32_t bitMask = 1 << flagIndex;
        ImVec4 themeColor = ColorValues.at(THEME_COLOR);
        ImVec4 colorDark = { themeColor.x * 0.4f, themeColor.y * 0.4f, themeColor.z * 0.4f, themeColor.z };
        ImVec4& color = themeColor;
        if (!hasDescription) {
            color = colorDark;
        }
        PushStyleCheckbox(hasDescription ? themeColor : colorDark);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
        bool flag = (flags & bitMask) != 0;
        if (ImGui::Checkbox("##check", &flag)) {
            if (flag) {
                flags |= bitMask;
            } else {
                flags &= ~bitMask;
            }
        }
        ImGui::PopStyleVar();
        PopStyleCheckbox();
        if (ImGui::IsItemHovered() && hasDescription) {
            ImGui::BeginTooltip();
            uint16_t index = row * 16 + flagIndex;
            const char* desc = flagTable.flagDescriptions.at(index);
            ImGui::Text("0x%02X: %s", index, UIWidgets::WrappedText(desc, 60).c_str());
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }

    ImGui::PopID();
}

static uint16_t& GetFlagTableEntry(const FlagTable& flagTable, size_t row) {
    switch (flagTable.flagTableType) {
        case EVENT_CHECK_INF:
            return gSaveContext.eventChkInf[row];
        case ITEM_GET_INF:
            return gSaveContext.itemGetInf[row];
        case INF_TABLE:
            return gSaveContext.infTable[row];
        case EVENT_INF:
            return gSaveContext.eventInf[row];
        case RANDOMIZER_INF:
            return gSaveContext.ship.randomizerInf[row];
        default: // Shouldn't be hit
            assert(false);
            return gSaveContext.eventChkInf[row];
    }
}

static void DrawFlagTableSearchResults(const FlagTable& flagTable, ImGuiTextFilter& filter) {
    bool hasMatches = false;

    for (size_t row = 0; row < flagTable.size + 1; row++) {
        uint16_t& flags = GetFlagTableEntry(flagTable, row);

        for (int32_t flagIndex = 15; flagIndex >= 0; flagIndex--) {
            uint16_t index = row * 16 + flagIndex;
            auto descIt = flagTable.flagDescriptions.find(index);
            const char* desc = descIt != flagTable.flagDescriptions.end() ? descIt->second : "";
            std::string searchable = fmt::format("0x{:02X} {}", index, desc);
            if (!filter.PassFilter(searchable.c_str())) {
                continue;
            }

            hasMatches = true;

            ImGui::PushID(index);
            bool hasDescription = descIt != flagTable.flagDescriptions.end();
            uint32_t bitMask = 1 << flagIndex;
            ImVec4 themeColor = ColorValues.at(THEME_COLOR);
            ImVec4 colorDark = { themeColor.x * 0.4f, themeColor.y * 0.4f, themeColor.z * 0.4f, themeColor.z };
            PushStyleCheckbox(hasDescription ? themeColor : colorDark);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
            bool flag = (flags & bitMask) != 0;
            if (ImGui::Checkbox("##check", &flag)) {
                if (flag) {
                    flags |= bitMask;
                } else {
                    flags &= ~bitMask;
                }
            }
            ImGui::PopStyleVar();
            PopStyleCheckbox();

            ImGui::SameLine();
            if (hasDescription) {
                ImGui::TextWrapped("0x%02X: %s", index, desc);
            } else {
                ImGui::Text("0x%02X", index);
            }

            ImGui::PopID();
        }
    }

    if (!hasMatches) {
        ImGui::Text("No flags match the current search.");
    }
}

void DrawFlagsTab() {
    if (ImGui::TreeNode("Player State")) {
        if (gPlayState != nullptr) {
            Player* player = GET_PLAYER(gPlayState);

            DrawSaveEditorGroup(
                [&]() {
                    ImGui::Text("stateFlags1");
                    DrawFlagArray32("stateFlags1", player->stateFlags1, THEME_COLOR);
                },
                "stateFlags1");

            ImGui::SameLine();

            DrawSaveEditorGroup(
                [&]() {
                    ImGui::Text("stateFlags2");
                    DrawFlagArray32("stateFlags2", player->stateFlags2, THEME_COLOR);
                },
                "stateFlags2");

            DrawSaveEditorGroup(
                [&]() {
                    ImGui::Text("stateFlags3");
                    DrawFlagArray8("stateFlags3", player->stateFlags3, THEME_COLOR);
                },
                "stateFlags3");

            ImGui::SameLine();

            DrawSaveEditorGroup(
                [&]() {
                    ImGui::Text("unk_6AE_rotFlags");
                    DrawFlagArray16("unk_6AE_rotFlags", player->unk_6AE_rotFlags, THEME_COLOR);
                },
                "unk_6AE_rotFlags");
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Current Scene")) {
        if (gPlayState != nullptr) {
            ActorContext* act = &gPlayState->actorCtx;
            DrawSaveEditorGroup(
                [&]() {
                    ImGui::Text("Switch");
                    InsertHelpHoverText("Permanently-saved switch flags");
                    if (Button("Set All##Switch", buttonOptionsBase.Tooltip(""))) {
                        act->flags.swch = UINT32_MAX;
                    }
                    ImGui::SameLine();
                    if (Button("Clear All##Switch", buttonOptionsBase.Tooltip(""))) {
                        act->flags.swch = 0;
                    }
                    DrawFlagArray32("Switch", act->flags.swch, THEME_COLOR);
                },
                "Switch");

            ImGui::SameLine();

            DrawSaveEditorGroup(
                [&]() {
                    ImGui::Text("Temp Switch");
                    InsertHelpHoverText("Temporary switch flags. Unset on scene transitions");
                    if (Button("Set All##Temp Switch", buttonOptionsBase.Tooltip(""))) {
                        act->flags.tempSwch = UINT32_MAX;
                    }
                    ImGui::SameLine();
                    if (Button("Clear All##Temp Switch", buttonOptionsBase.Tooltip(""))) {
                        act->flags.tempSwch = 0;
                    }
                    DrawFlagArray32("Temp Switch", act->flags.tempSwch, THEME_COLOR);
                },
                "Temp Switch");

            DrawSaveEditorGroup(
                [&]() {
                    ImGui::Text("Clear");
                    InsertHelpHoverText("Permanently-saved room-clear flags");
                    if (Button("Set All##Clear", buttonOptionsBase.Tooltip(""))) {
                        act->flags.clear = UINT32_MAX;
                    }
                    ImGui::SameLine();
                    if (Button("Clear All##Clear", buttonOptionsBase.Tooltip(""))) {
                        act->flags.clear = 0;
                    }
                    DrawFlagArray32("Clear", act->flags.clear, THEME_COLOR);
                },
                "Clear");

            ImGui::SameLine();

            DrawSaveEditorGroup(
                [&]() {
                    ImGui::Text("Temp Clear");
                    InsertHelpHoverText("Temporary room-clear flags. Unset on scene transitions");
                    if (Button("Set All##Temp Clear", buttonOptionsBase.Tooltip(""))) {
                        act->flags.tempClear = UINT32_MAX;
                    }
                    ImGui::SameLine();
                    if (Button("Clear All##Temp Clear", buttonOptionsBase.Tooltip(""))) {
                        act->flags.tempClear = 0;
                    }
                    DrawFlagArray32("Temp Clear", act->flags.tempClear, THEME_COLOR);
                },
                "Temp Clear");

            DrawSaveEditorGroup(
                [&]() {
                    ImGui::Text("Collect");
                    InsertHelpHoverText("Permanently-saved collect flags");
                    if (Button("Set All##Collect", buttonOptionsBase.Tooltip(""))) {
                        act->flags.collect = UINT32_MAX;
                    }
                    ImGui::SameLine();
                    if (Button("Clear All##Collect", buttonOptionsBase.Tooltip(""))) {
                        act->flags.collect = 0;
                    }
                    DrawFlagArray32("Collect", act->flags.collect, THEME_COLOR);
                },
                "Collect");

            ImGui::SameLine();

            DrawSaveEditorGroup(
                [&]() {
                    ImGui::Text("Temp Collect");
                    InsertHelpHoverText("Temporary collect flags. Unset on scene transitions");
                    if (Button("Set All##Temp Collect", buttonOptionsBase.Tooltip(""))) {
                        act->flags.tempCollect = UINT32_MAX;
                    }
                    ImGui::SameLine();
                    if (Button("Clear All##Temp Collect", buttonOptionsBase.Tooltip(""))) {
                        act->flags.tempCollect = 0;
                    }
                    DrawFlagArray32("Temp Collect", act->flags.tempCollect, THEME_COLOR);
                },
                "Temp Collect");

            DrawSaveEditorGroup(
                [&]() {
                    ImGui::Text("Chest");
                    InsertHelpHoverText("Permanently-saved chest flags");
                    if (Button("Set All##Chest", buttonOptionsBase.Tooltip(""))) {
                        act->flags.chest = UINT32_MAX;
                    }
                    ImGui::SameLine();
                    if (Button("Clear All##Chest", buttonOptionsBase.Tooltip(""))) {
                        act->flags.chest = 0;
                    }
                    DrawFlagArray32("Chest", act->flags.chest, THEME_COLOR);
                },
                "Chest");

            ImGui::SameLine();

            ImGui::BeginGroup();

            if (Button("Reload Flags", buttonOptionsBase.Tooltip(
                                           "Load flags from saved scene flags. Normally happens on scene load"))) {
                act->flags.swch = gSaveContext.sceneFlags[gPlayState->sceneNum].swch;
                act->flags.clear = gSaveContext.sceneFlags[gPlayState->sceneNum].clear;
                act->flags.collect = gSaveContext.sceneFlags[gPlayState->sceneNum].collect;
                act->flags.chest = gSaveContext.sceneFlags[gPlayState->sceneNum].chest;
            }

            if (Button("Save Flags",
                       buttonOptionsBase.Tooltip("Save current scene flags. Normally happens on scene exit"))) {
                gSaveContext.sceneFlags[gPlayState->sceneNum].swch = act->flags.swch;
                gSaveContext.sceneFlags[gPlayState->sceneNum].clear = act->flags.clear;
                gSaveContext.sceneFlags[gPlayState->sceneNum].collect = act->flags.collect;
                gSaveContext.sceneFlags[gPlayState->sceneNum].chest = act->flags.chest;
            }

            if (Button("Clear Flags",
                       buttonOptionsBase.Tooltip("Clear current scene flags. Reload scene to see changes"))) {
                act->flags.swch = 0;
                act->flags.clear = 0;
                act->flags.collect = 0;
                act->flags.chest = 0;
            }

            ImGui::EndGroup();
        } else {
            ImGui::Text("Current game state does not have an active scene");
        }

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Saved Scene Flags")) {
        static uint32_t selectedSceneFlagMap = 0;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Map");
        ImGui::SameLine();
        PushStyleCombobox(THEME_COLOR);
        if (ImGui::BeginCombo("##Map", SohUtils::GetSceneName(selectedSceneFlagMap).c_str())) {
            for (int32_t sceneIndex = 0; sceneIndex < SCENE_ID_MAX; sceneIndex++) {
                if (ImGui::Selectable(SohUtils::GetSceneName(sceneIndex).c_str())) {
                    selectedSceneFlagMap = sceneIndex;
                }
            }

            ImGui::EndCombo();
        }
        PopStyleCombobox();

        // Don't show current scene button if there is no current scene
        if (gPlayState != nullptr) {
            ImGui::SameLine();
            if (Button("Current", buttonOptionsBase.Tooltip("Open flags for current scene"))) {
                selectedSceneFlagMap = gPlayState->sceneNum;
            }
        }

        DrawSaveEditorGroup(
            [&]() {
                ImGui::Text("Switch");
                InsertHelpHoverText("Switch flags");
                DrawFlagArray32("Switch", gSaveContext.sceneFlags[selectedSceneFlagMap].swch, THEME_COLOR);
            },
            "Saved Switch");

        ImGui::SameLine();

        DrawSaveEditorGroup(
            [&]() {
                ImGui::Text("Clear");
                InsertHelpHoverText("Room-clear flags");
                DrawFlagArray32("Clear", gSaveContext.sceneFlags[selectedSceneFlagMap].clear, THEME_COLOR);
            },
            "Saved Clear");

        DrawSaveEditorGroup(
            [&]() {
                ImGui::Text("Collect");
                InsertHelpHoverText("Collect flags");
                DrawFlagArray32("Collect", gSaveContext.sceneFlags[selectedSceneFlagMap].collect, THEME_COLOR);
            },
            "Saved Collect");

        ImGui::SameLine();

        DrawSaveEditorGroup(
            [&]() {
                ImGui::Text("Chest");
                InsertHelpHoverText("Chest flags");
                DrawFlagArray32("Chest", gSaveContext.sceneFlags[selectedSceneFlagMap].chest, THEME_COLOR);
            },
            "Saved Chest");

        DrawSaveEditorGroup(
            [&]() {
                ImGui::Text("Rooms");
                InsertHelpHoverText("Flags for visted rooms");
                DrawFlagArray32("Rooms", gSaveContext.sceneFlags[selectedSceneFlagMap].rooms, THEME_COLOR);
            },
            "Saved Rooms");

        ImGui::SameLine();

        DrawSaveEditorGroup(
            [&]() {
                ImGui::Text("Floors");
                InsertHelpHoverText("Flags for visted floors");
                DrawFlagArray32("Floors", gSaveContext.sceneFlags[selectedSceneFlagMap].floors, THEME_COLOR);
            },
            "Saved Floors");

        ImGui::TreePop();
    }

    DrawSaveEditorGroup(
        [&]() {
            PushStyleCombobox(THEME_COLOR);
            static size_t selectedGsMap = 0;
            ImGui::Text("Gold Skulltulas");
            if (ImGui::BeginCombo("##GSMap", gsMapping[selectedGsMap])) {
                for (size_t index = 0; index < gsMapping.size(); index++) {
                    if (ImGui::Selectable(gsMapping[index])) {
                        selectedGsMap = index;
                    }
                }

                ImGui::EndCombo();
            }
            PopStyleCombobox();

            // TODO We should write out descriptions for each one... ugh
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Flags");
            uint32_t currentFlags = GET_GS_FLAGS(selectedGsMap);
            uint32_t allFlags = gAreaGsFlags[selectedGsMap];
            uint32_t setMask = 1;
            // Iterate over bitfield and create a checkbox for each skulltula
            while (allFlags != 0) {
                bool isThisSet = (currentFlags & 0x1) == 0x1;

                ImGui::SameLine();
                ImGui::PushID(allFlags);
                PushStyleCheckbox(THEME_COLOR);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
                if (ImGui::Checkbox("##gs", &isThisSet)) {
                    if (isThisSet) {
                        SET_GS_FLAGS(selectedGsMap, setMask);
                    } else {
                        // Have to do this roundabout method as the macro does not support clearing flags
                        uint32_t currentFlagsBase = GET_GS_FLAGS(selectedGsMap);
                        gSaveContext.gsFlags[selectedGsMap >> 2] &= ~gGsFlagsMasks[selectedGsMap & 3];
                        SET_GS_FLAGS(selectedGsMap, currentFlagsBase & ~setMask);
                    }
                }
                ImGui::PopStyleVar();
                PopStyleCheckbox();

                ImGui::PopID();

                allFlags >>= 1;
                currentFlags >>= 1;
                setMask <<= 1;
            }

            // If playing a Randomizer Save with Shuffle Skull Tokens on anything other than "Off" we don't want to keep
            // GS Token Count updated, since Gold Skulltulas killed will not correlate to GS Tokens Collected.
            if (!(IS_RANDO &&
                  OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_SHUFFLE_TOKENS) != RO_TOKENSANITY_OFF)) {
                static bool keepGsCountUpdated = true;
                Checkbox("Keep GS Count Updated", &keepGsCountUpdated,
                         checkboxOptionsBase.Tooltip(
                             "Automatically adjust the number of gold skulltula tokens acquired based on set flags."));
                int32_t gsCount = 0;
                if (keepGsCountUpdated) {
                    for (int32_t gsFlagIndex = 0; gsFlagIndex < 6; gsFlagIndex++) {
                        gsCount += std::popcount(static_cast<uint32_t>(gSaveContext.gsFlags[gsFlagIndex]));
                    }
                    gSaveContext.inventory.gsTokens = gsCount;
                }
            }
        },
        "Gold Skulltulas");

    for (size_t i = 0; i < flagTables.size(); i++) {
        const FlagTable& flagTable = flagTables[i];
        if (flagTable.flagTableType == RANDOMIZER_INF && !IS_RANDO && !IS_BOSS_RUSH) {
            continue;
        }

        if (ImGui::TreeNode(flagTable.name)) {
            ImGui::PushID(flagTable.name);
            ImGuiTextFilter& flagFilter = flagTableFilters[flagTable.name];
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16);
            PushStyleInput(THEME_COLOR);
            flagFilter.Draw();
            PopStyleInput();
            ImGui::Spacing();

            if (!flagFilter.IsActive()) {
                for (size_t j = 0; j < flagTable.size + 1; j++) {
                    DrawSaveEditorGroup(
                        [&]() {
                            if (j == 0) {
                                for (int k = 0xF; k >= 0; k--) {
                                    ImGui::SameLine(static_cast<f32>(37.5 + ((0xF - k) * 33.8)));
                                    ImGui::Text("%X", k);
                                }
                            }

                            ImGui::Text("%s", fmt::format("{:<2X}", j).c_str());

                            switch (flagTable.flagTableType) {
                                case EVENT_CHECK_INF:
                                    DrawFlagTableArray16(flagTable, j, gSaveContext.eventChkInf[j]);
                                    break;
                                case ITEM_GET_INF:
                                    DrawFlagTableArray16(flagTable, j, gSaveContext.itemGetInf[j]);
                                    break;
                                case INF_TABLE:
                                    DrawFlagTableArray16(flagTable, j, gSaveContext.infTable[j]);
                                    break;
                                case EVENT_INF:
                                    DrawFlagTableArray16(flagTable, j, gSaveContext.eventInf[j]);
                                    break;
                                case RANDOMIZER_INF:
                                    DrawFlagTableArray16(flagTable, j, gSaveContext.ship.randomizerInf[j]);
                                    break;
                            }
                        },
                        flagTable.name);
                }
            } else {
                DrawFlagTableSearchResults(flagTable, flagFilter);
            }

            // make some buttons to help with fishsanity debugging
            uint8_t fsMode = OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_FISHSANITY);
            if (flagTable.flagTableType == RANDOMIZER_INF && fsMode != RO_FISHSANITY_OFF &&
                fsMode != RO_FISHSANITY_OVERWORLD) {
                if (ImGui::Button("Catch All (Child)")) {
                    for (int k = RAND_INF_CHILD_FISH_1; k <= RAND_INF_CHILD_LOACH_2; k++) {
                        Flags_SetRandomizerInf((RandomizerInf)k);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Uncatch All (Child)")) {
                    for (int k = RAND_INF_CHILD_FISH_1; k <= RAND_INF_CHILD_LOACH_2; k++) {
                        Flags_UnsetRandomizerInf((RandomizerInf)k);
                    }
                }

                if (ImGui::Button("Catch All (Adult)")) {
                    for (int k = RAND_INF_ADULT_FISH_1; k <= RAND_INF_ADULT_LOACH; k++) {
                        Flags_SetRandomizerInf((RandomizerInf)k);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Uncatch All (Adult)")) {
                    for (int k = RAND_INF_ADULT_FISH_1; k <= RAND_INF_ADULT_LOACH; k++) {
                        Flags_UnsetRandomizerInf((RandomizerInf)k);
                    }
                }
            }

            ImGui::PopID();
            ImGui::TreePop();
        }
    }
}

// Draws a combo that lets you choose and upgrade value from a drop-down of text values
void DrawUpgrade(const std::string& categoryName, int32_t categoryId, const std::vector<std::string>& names) {
    ImGui::Text("%s", categoryName.c_str());
    ImGui::SameLine();
    ImGui::PushID(categoryName.c_str());
    PushStyleCombobox(THEME_COLOR);
    ImGui::AlignTextToFramePadding();
    auto value = (size_t)CUR_UPG_VALUE(categoryId);
    auto name = value < names.size() ? names[value].c_str() : "Glitched";
    if (ImGui::BeginCombo("##upgrade", name)) {
        for (size_t i = 0; i < names.size(); i++) {
            if (ImGui::Selectable(names[i].c_str())) {
                Inventory_ChangeUpgrade(categoryId, i);
            }
        }

        ImGui::EndCombo();
    }
    PopStyleCombobox();
    ImGui::PopID();
    UIWidgets::Tooltip(categoryName.c_str());
}

// Draws a combo that lets you choose and upgrade value from a popup grid of icons
void DrawUpgradeIcon(const std::string& categoryName, int32_t categoryId, const std::vector<uint8_t>& items) {
    static const char* upgradePopupPicker = "upgradePopupPicker";

    ImGui::PushID(categoryName.c_str());

    PushStyleButton(Colors::DarkGray);
    auto value = (size_t)CUR_UPG_VALUE(categoryId);
    uint8_t item = value < items.size() ? items[value] : ITEM_NONE;
    if (item != ITEM_NONE) {
        const ItemMapEntry& slotEntry = itemMapping[item];
        if (ImGui::ImageButton(
                slotEntry.name.c_str(),
                std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                    ->GetTextureByName(slotEntry.name),
                ImVec2(IMAGE_SIZE, IMAGE_SIZE), ImVec2(0, 0), ImVec2(1, 1))) {
            ImGui::OpenPopup(upgradePopupPicker);
        }
    } else {
        if (ImGui::Button("##itemNone", ImVec2(IMAGE_SIZE, IMAGE_SIZE) + ImGui::GetStyle().FramePadding * 2)) {
            ImGui::OpenPopup(upgradePopupPicker);
        }
    }
    PopStyleButton();
    Tooltip(categoryName.c_str());

    if (ImGui::BeginPopup(upgradePopupPicker)) {
        for (size_t pickerIndex = 0; pickerIndex < items.size(); pickerIndex++) {
            if ((pickerIndex % 8) != 0) {
                ImGui::SameLine();
            }

            PushStyleButton(Colors::DarkGray);
            if (items[pickerIndex] == ITEM_NONE) {
                if (ImGui::Button("##upgradePopupPicker",
                                  ImVec2(IMAGE_SIZE, IMAGE_SIZE) + ImGui::GetStyle().FramePadding * 2)) {
                    Inventory_ChangeUpgrade(categoryId, pickerIndex);
                    ImGui::CloseCurrentPopup();
                }
                Tooltip("None");
            } else {
                const ItemMapEntry& slotEntry = itemMapping[items[pickerIndex]];
                auto ret = ImGui::ImageButton(
                    slotEntry.name.c_str(),
                    std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                        ->GetTextureByName(slotEntry.name),
                    ImVec2(IMAGE_SIZE, IMAGE_SIZE), ImVec2(0, 0), ImVec2(1, 1));
                if (ret) {
                    Inventory_ChangeUpgrade(categoryId, pickerIndex);
                    ImGui::CloseCurrentPopup();
                }
                Tooltip(SohUtils::GetItemName(slotEntry.id).c_str());
            }
            PopStyleButton();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void DrawEquipmentTab() {
    const std::vector<uint8_t> equipmentValues = {
        ITEM_SWORD_KOKIRI, ITEM_SWORD_MASTER,  ITEM_SWORD_BGS,     ITEM_SWORD_BROKEN,
        ITEM_SHIELD_DEKU,  ITEM_SHIELD_HYLIAN, ITEM_SHIELD_MIRROR, ITEM_NONE,
        ITEM_TUNIC_KOKIRI, ITEM_TUNIC_GORON,   ITEM_TUNIC_ZORA,    ITEM_NONE,
        ITEM_BOOTS_KOKIRI, ITEM_BOOTS_IRON,    ITEM_BOOTS_HOVER,   ITEM_NONE,
    };
    for (size_t i = 0; i < equipmentValues.size(); i++) {
        // Skip over unused 4th slots for shields, boots, and tunics
        if (equipmentValues[i] == ITEM_NONE) {
            continue;
        }
        if ((i % 4) != 0) {
            ImGui::SameLine();
        }

        ImGui::PushID(i);
        uint32_t bitMask = 1 << i;
        bool hasEquip = (bitMask & gSaveContext.inventory.equipment) != 0;
        const ItemMapEntry& entry = itemMapping[equipmentValues[i]];
        PushStyleButton(Colors::DarkGray);
        auto ret = ImGui::ImageButton(
            entry.name.c_str(),
            std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                ->GetTextureByName(hasEquip ? entry.name : entry.nameFaded),
            ImVec2(IMAGE_SIZE, IMAGE_SIZE), ImVec2(0, 0), ImVec2(1, 1));
        if (ret) {
            if (hasEquip) {
                gSaveContext.inventory.equipment &= ~bitMask;
            } else {
                gSaveContext.inventory.equipment |= bitMask;
            }
        }
        PopStyleButton();
        Tooltip(SohUtils::GetItemName(entry.id).c_str());
        ImGui::PopID();
    }

    const std::vector<uint8_t> bulletBagValues = {
        ITEM_NONE,
        ITEM_BULLET_BAG_30,
        ITEM_BULLET_BAG_40,
        ITEM_BULLET_BAG_50,
    };
    DrawUpgradeIcon("Bullet Bag", UPG_BULLET_BAG, bulletBagValues);

    ImGui::SameLine();

    const std::vector<uint8_t> quiverValues = {
        ITEM_NONE,
        ITEM_QUIVER_30,
        ITEM_QUIVER_40,
        ITEM_QUIVER_50,
    };
    DrawUpgradeIcon("Quiver", UPG_QUIVER, quiverValues);

    ImGui::SameLine();

    const std::vector<uint8_t> bombBagValues = {
        ITEM_NONE,
        ITEM_BOMB_BAG_20,
        ITEM_BOMB_BAG_30,
        ITEM_BOMB_BAG_40,
    };
    DrawUpgradeIcon("Bomb Bag", UPG_BOMB_BAG, bombBagValues);

    ImGui::SameLine();

    const std::vector<uint8_t> scaleValues = {
        ITEM_NONE,
        ITEM_SCALE_SILVER,
        ITEM_SCALE_GOLDEN,
    };
    DrawUpgradeIcon("Scale", UPG_SCALE, scaleValues);

    ImGui::SameLine();

    const std::vector<uint8_t> strengthValues = {
        ITEM_NONE,
        ITEM_BRACELET,
        ITEM_GAUNTLETS_SILVER,
        ITEM_GAUNTLETS_GOLD,
    };
    DrawUpgradeIcon("Strength", UPG_STRENGTH, strengthValues);

    // There is no icon for child wallet, so default to a text list
    // this was const, but I needed to append to it depending in rando settings.
    std::vector<std::string> walletNamesImpl = {
        "Child (99)",
        "Adult (200)",
        "Giant (500)",
    };
    // only display Tycoon wallet if you're in a save file that would allow it.
    if (IS_RANDO && OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_INCLUDE_TYCOON_WALLET)) {
        const std::string walletName = "Tycoon (999)";
        walletNamesImpl.push_back(walletName);
    }
    // copy it to const value for display in ImGui.
    const std::vector<std::string> walletNames = walletNamesImpl;
    DrawUpgrade("Wallet", UPG_WALLET, walletNames);

    const std::vector<std::string> stickNames = {
        "None",
        "10",
        "20",
        "30",
    };
    DrawUpgrade("Deku Stick Capacity", UPG_STICKS, stickNames);

    const std::vector<std::string> nutNames = {
        "None",
        "20",
        "30",
        "40",
    };
    DrawUpgrade("Deku Nut Capacity", UPG_NUTS, nutNames);

    if (IS_RANDO &&
        OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_BOMBCHU_BAG) == RO_BOMBCHU_BAG_PROGRESSIVE) {
        const std::vector<std::string> bombchuNames = {
            "None",
            "20",
            "30",
            "50",
        };
        ImGui::Text("%s", "Bombchu Bag Capacity");
        ImGui::SameLine();
        ImGui::PushID("Bombchu Bag Capacity");
        PushStyleCombobox(THEME_COLOR);
        ImGui::AlignTextToFramePadding();
        auto value = gSaveContext.ship.quest.data.randomizer.bombchuUpgradeLevel;
        auto name = value < bombchuNames.size() ? bombchuNames[value].c_str() : "Glitched";
        if (ImGui::BeginCombo("##upgrade", name)) {
            for (size_t i = 0; i < bombchuNames.size(); i++) {
                if (ImGui::Selectable(bombchuNames[i].c_str())) {
                    gSaveContext.ship.quest.data.randomizer.bombchuUpgradeLevel = i;
                    if (i > 0) {
                        INV_CONTENT(ITEM_BOMBCHU) = ITEM_BOMBCHU;
                    } else {
                        INV_CONTENT(ITEM_BOMBCHU) = ITEM_NONE;
                    }
                }
            }
            ImGui::EndCombo();
        }
        PopStyleCombobox();
        ImGui::PopID();
        UIWidgets::Tooltip("Bombchu Bag Capapcity");
    }
}

// Draws a toggleable icon for a quest item that is faded when disabled
void DrawQuestItemButton(uint32_t item) {
    const QuestMapEntry& entry = questMapping[item];
    uint32_t bitMask = 1 << entry.id;
    bool hasQuestItem = (bitMask & gSaveContext.inventory.questItems) != 0;
    PushStyleButton(Colors::DarkGray);
    auto ret = ImGui::ImageButton(
        entry.name.c_str(),
        std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
            ->GetTextureByName(hasQuestItem ? entry.name : entry.nameFaded),
        ImVec2(IMAGE_SIZE, IMAGE_SIZE), ImVec2(0, 0), ImVec2(1, 1));
    if (ret) {
        if (hasQuestItem) {
            gSaveContext.inventory.questItems &= ~bitMask;
        } else {
            gSaveContext.inventory.questItems |= bitMask;
        }
    }
    PopStyleButton();
    Tooltip(SohUtils::GetQuestItemName(entry.id).c_str());
}

// Draws a toggleable icon for a dungeon item that is faded when disabled
void DrawDungeonItemButton(uint32_t item, uint32_t scene) {
    const ItemMapEntry& entry = itemMapping[item];
    uint32_t bitMask = 1 << (entry.id - ITEM_KEY_BOSS); // Bitset starts at ITEM_KEY_BOSS == 0. the rest are sequential
    bool hasItem = (bitMask & gSaveContext.inventory.dungeonItems[scene]) != 0;
    PushStyleButton(Colors::DarkGray);
    auto ret = ImGui::ImageButton(
        entry.name.c_str(),
        std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
            ->GetTextureByName(hasItem ? entry.name : entry.nameFaded),
        ImVec2(IMAGE_SIZE, IMAGE_SIZE), ImVec2(0, 0), ImVec2(1, 1));
    if (ret) {
        if (hasItem) {
            gSaveContext.inventory.dungeonItems[scene] &= ~bitMask;
        } else {
            gSaveContext.inventory.dungeonItems[scene] |= bitMask;
        }
    }
    PopStyleButton();
    Tooltip(SohUtils::GetItemName(entry.id).c_str());
}

void DrawQuestStatusTab() {

    for (int32_t i = QUEST_MEDALLION_FOREST; i < QUEST_MEDALLION_LIGHT + 1; i++) {
        if (i != QUEST_MEDALLION_FOREST) {
            ImGui::SameLine();
        }
        DrawQuestItemButton(i);
    }

    for (int32_t i = QUEST_KOKIRI_EMERALD; i < QUEST_ZORA_SAPPHIRE + 1; i++) {
        if (i != QUEST_KOKIRI_EMERALD) {
            ImGui::SameLine();
        }
        DrawQuestItemButton(i);
    }

    // Put Stone of Agony and Gerudo Card on the same line with a little space between them
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(IMAGE_SIZE, IMAGE_SIZE) + ImGui::GetStyle().FramePadding * 2);

    ImGui::SameLine();
    DrawQuestItemButton(QUEST_STONE_OF_AGONY);

    ImGui::SameLine();
    DrawQuestItemButton(QUEST_GERUDO_CARD);
    for (const auto& [quest, entry] : songMapping) {
        if ((entry.id != QUEST_SONG_MINUET) && (entry.id != QUEST_SONG_LULLABY)) {
            ImGui::SameLine();
        }

        uint32_t bitMask = 1 << entry.id;
        bool hasQuestItem = (bitMask & gSaveContext.inventory.questItems) != 0;
        PushStyleButton(Colors::DarkGray);
        auto ret = ImGui::ImageButton(
            entry.name.c_str(),
            std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                ->GetTextureByName(hasQuestItem ? entry.name : entry.nameFaded),
            ImVec2(32.0f, 48.0f), ImVec2(0, 0), ImVec2(1, 1));
        if (ret) {
            if (hasQuestItem) {
                gSaveContext.inventory.questItems &= ~bitMask;
            } else {
                gSaveContext.inventory.questItems |= bitMask;
            }
        }
        PopStyleButton();
        Tooltip(SohUtils::GetQuestItemName(entry.id).c_str());
    }

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("GS Count", ImGuiDataType_S16, &gSaveContext.inventory.gsTokens);
    PopStyleInput();
    InsertHelpHoverText("Number of gold skulltula tokens aquired");

    uint32_t bitMask = 1 << QUEST_SKULL_TOKEN;
    bool gsUnlocked = (bitMask & gSaveContext.inventory.questItems) != 0;
    if (Checkbox("GS unlocked", &gsUnlocked, CheckboxOptions().Color(THEME_COLOR))) {
        if (gsUnlocked) {
            gSaveContext.inventory.questItems |= bitMask;
        } else {
            gSaveContext.inventory.questItems &= ~bitMask;
        }
    }
    InsertHelpHoverText("If unlocked, enables showing the gold skulltula count in the quest status menu");

    int32_t pohCount = (gSaveContext.inventory.questItems & 0xF0000000) >> 28;
    PushStyleCombobox(THEME_COLOR);
    if (ImGui::BeginCombo("PoH count", std::to_string(pohCount).c_str())) {
        for (int32_t i = 0; i < 4; i++) {
            if (ImGui::Selectable(std::to_string(i).c_str(), pohCount == i)) {
                gSaveContext.inventory.questItems &= ~0xF0000000;
                gSaveContext.inventory.questItems |= (i << 28);
            }
        }
        ImGui::EndCombo();
    }
    InsertHelpHoverText("The number of pieces of heart acquired towards the next heart container");
    PopStyleCombobox();

    DrawSaveEditorGroup(
        [&]() {
            ImGui::Text("Dungeon Items");

            static int32_t dungeonItemsScene = SCENE_DEKU_TREE;
            static int32_t lastDungeonScene = -1;
            if (gPlayState != nullptr) {
                int32_t sceneNum = gPlayState->sceneNum;
                if (sceneNum >= SCENE_DEKU_TREE && sceneNum <= SCENE_JABU_JABU_BOSS && lastDungeonScene != sceneNum) {
                    dungeonItemsScene = sceneNum;
                    lastDungeonScene = sceneNum;
                }
            }

            PushStyleCombobox(THEME_COLOR);
            if (ImGui::BeginCombo("##DungeonSelect", SohUtils::GetSceneName(dungeonItemsScene).c_str())) {
                for (int32_t dungeonIndex = SCENE_DEKU_TREE; dungeonIndex < SCENE_JABU_JABU_BOSS + 1; dungeonIndex++) {
                    if (ImGui::Selectable(SohUtils::GetSceneName(dungeonIndex).c_str(),
                                          dungeonIndex == dungeonItemsScene)) {
                        dungeonItemsScene = dungeonIndex;
                    }
                }

                ImGui::EndCombo();
            }
            PopStyleCombobox();

            DrawDungeonItemButton(ITEM_KEY_BOSS, dungeonItemsScene);
            ImGui::SameLine();
            DrawDungeonItemButton(ITEM_COMPASS, dungeonItemsScene);
            ImGui::SameLine();
            DrawDungeonItemButton(ITEM_DUNGEON_MAP, dungeonItemsScene);

            if (dungeonItemsScene != SCENE_JABU_JABU_BOSS) {
                float lineHeight = ImGui::GetTextLineHeightWithSpacing();
                ImGui::Image(
                    std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                        ->GetTextureByName(itemMapping[ITEM_KEY_SMALL].name),
                    ImVec2(lineHeight, lineHeight));
                ImGui::SameLine();
                PushStyleInput(THEME_COLOR);
                if (ImGui::InputScalar("##Keys", ImGuiDataType_S8,
                                       gSaveContext.inventory.dungeonKeys + dungeonItemsScene)) {
                    gSaveContext.ship.stats.dungeonKeys[dungeonItemsScene] =
                        gSaveContext.inventory.dungeonKeys[dungeonItemsScene];
                };
                PopStyleInput();
            } else {
                // dungeonItems is size 20 but dungeonKeys is size 19, so there are no keys for the last scene
                // (Barinade's Lair)
                ImGui::Text("Barinade's Lair does not have small keys");
            }
        },
        "Dungeon Items");
}

void ResetBaseOptions() {
    intSliderOptionsBase.Color(THEME_COLOR).Size({ 320.0f, 0.0f }).Tooltip("");
    buttonOptionsBase.Color(THEME_COLOR).Size(Sizes::Inline).Tooltip("");
    checkboxOptionsBase.Color(THEME_COLOR).Tooltip("");
    comboboxOptionsBase.Color(THEME_COLOR)
        .ComponentAlignment(ComponentAlignments::Left)
        .LabelPosition(LabelPositions::Near)
        .Tooltip("");
}

void SaveEditorWindow::DrawElement() {
    PushStyleTabs(THEME_COLOR);
    ImGui::PushFont(OTRGlobals::Instance->fontMonoLarger);
    ImGui::BeginDisabled(CVarGetInteger(CVAR_SETTING("DisableChanges"), 0));

    if (ImGui::BeginTabBar("SaveContextTabBar", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)) {
        ResetBaseOptions();
        if (ImGui::BeginTabItem("Info")) {
            DrawInfoTab();
            ImGui::EndTabItem();
        }

        ResetBaseOptions();
        if (ImGui::BeginTabItem("Inventory")) {
            DrawInventoryTab();
            ImGui::EndTabItem();
        }

        ResetBaseOptions();
        if (ImGui::BeginTabItem("Flags")) {
            DrawFlagsTab();
            ImGui::EndTabItem();
        }

        ResetBaseOptions();
        if (ImGui::BeginTabItem("Equipment")) {
            DrawEquipmentTab();
            ImGui::EndTabItem();
        }

        ResetBaseOptions();
        if (ImGui::BeginTabItem("Quest Status")) {
            DrawQuestStatusTab();
            ImGui::EndTabItem();
        }

        ResetBaseOptions();
        if (ImGui::BeginTabItem("Player")) {
            DrawPlayerTab();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::EndDisabled();
    ImGui::PopFont();
    PopStyleTabs();
}

void SaveEditorWindow::InitElement() {
    std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
        ->LoadGuiTexture("ROCS_FEATHER", gRocsFeatherTex, Ship::Color4f(1, 1, 1, 1));
}
