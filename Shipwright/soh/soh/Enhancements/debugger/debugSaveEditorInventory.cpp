#include "debugSaveEditorInternal.h"

#include "soh/OTRGlobals.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/util.h"
#include "soh/SohGui/ImGuiUtils.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SaveManager.h"

#include <array>
#include <bit>
#include <map>
#include <string>
#include <vector>

#include <libultraship/bridge.h>
#include <libultraship/libultraship.h>
#include <soh_assets.h>
#include <spdlog/fmt/fmt.h>

#include <fast/Fast3dGui.h>

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions/game_state.h"
#include "macros.h"
#include "soh/cvar_prefixes.h"
extern PlayState* gPlayState;
}

using namespace UIWidgets;

extern "C" u8 gAmmoItems[];

void DrawBGSItemFlag(uint8_t itemID) {
    const ItemMapEntry& slotEntry = itemMapping[itemID];
    ImGui::Image(std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                     ->GetTextureByName(slotEntry.name),
                 ImVec2(32.0f, 32.0f), ImVec2(0, 0), ImVec2(1, 1));
}

void DrawInventoryTab() {
    static bool restrictToValid = true;

    Checkbox(
        "Restrict to valid items", &restrictToValid,
        checkboxOptionsBase.Tooltip("Restricts items and ammo to only what is possible to legally acquire in-game"));

    for (int32_t y = 0; y < 4; y++) {
        for (int32_t x = 0; x < 6; x++) {
            int32_t index = x + y * 6;
            static const char* itemPopupPicker = "itemPopupPicker";

            ImGui::PushID(index);

            if (x != 0) {
                ImGui::SameLine();
            }

            uint8_t item = gSaveContext.inventory.items[index];
            PushStyleButton(Colors::DarkGray);
            if (item == ITEM_ROCS_FEATHER) {
                auto ret = ImGui::ImageButton(
                    "ROCS_FEATHER",
                    std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                        ->GetTextureByName("ROCS_FEATHER"),
                    ImVec2(48.0f, 48.0f), ImVec2(0, 0), ImVec2(1, 1));
                if (ret) {
                    ImGui::OpenPopup(itemPopupPicker);
                }
            } else if (item != ITEM_NONE) {
                const ItemMapEntry& slotEntry = itemMapping.find(item)->second;
                auto ret = ImGui::ImageButton(
                    slotEntry.name.c_str(),
                    std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                        ->GetTextureByName(slotEntry.name),
                    ImVec2(48.0f, 48.0f), ImVec2(0, 0), ImVec2(1, 1));
                if (ret) {
                    ImGui::OpenPopup(itemPopupPicker);
                }
            } else {
                if (ImGui::Button("##itemNone", ImVec2(IMAGE_SIZE, IMAGE_SIZE) + ImGui::GetStyle().FramePadding * 2)) {
                    ImGui::OpenPopup(itemPopupPicker);
                }
            }
            PopStyleButton();

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            if (ImGui::BeginPopup(itemPopupPicker)) {
                PushStyleButton(Colors::DarkGray);
                if (ImGui::Button("##itemNonePicker",
                                  ImVec2(IMAGE_SIZE, IMAGE_SIZE) + ImGui::GetStyle().FramePadding * 2)) {
                    gSaveContext.inventory.items[index] = ITEM_NONE;
                    ImGui::CloseCurrentPopup();
                }
                PopStyleButton();
                UIWidgets::Tooltip("None");

                std::vector<ItemMapEntry> possibleItems;
                if (restrictToValid) {
                    // Scan gItemSlots to find legal items for this slot. Bottles are a special case
                    for (int slotIndex = 0; slotIndex < 56; slotIndex++) {
                        int testIndex = (index == SLOT_BOTTLE_1 || index == SLOT_BOTTLE_2 || index == SLOT_BOTTLE_3 ||
                                         index == SLOT_BOTTLE_4)
                                            ? SLOT_BOTTLE_1
                                            : index;
                        if (gItemSlots[slotIndex] == testIndex) {
                            possibleItems.push_back(itemMapping[slotIndex]);
                        }
                    }
                } else {
                    for (const auto& entry : itemMapping) {
                        possibleItems.push_back(entry.second);
                    }
                }

                for (size_t pickerIndex = 0; pickerIndex < possibleItems.size(); pickerIndex++) {
                    if (((pickerIndex + 1) % 8) != 0) {
                        ImGui::SameLine();
                    }
                    const ItemMapEntry& slotEntry = possibleItems[pickerIndex];
                    PushStyleButton(Colors::DarkGray);
                    auto ret = ImGui::ImageButton(slotEntry.name.c_str(),
                                                  std::dynamic_pointer_cast<Fast::Fast3dGui>(
                                                      Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                                                      ->GetTextureByName(slotEntry.name),
                                                  ImVec2(IMAGE_SIZE, IMAGE_SIZE), ImVec2(0, 0), ImVec2(1, 1));
                    PopStyleButton();
                    if (ret) {
                        gSaveContext.inventory.items[index] = slotEntry.id;
                        ImGui::CloseCurrentPopup();
                    }
                    UIWidgets::Tooltip(SohUtils::GetItemName(slotEntry.id).c_str());
                }

                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();

            ImGui::PopID();
        }
    }

    ImGui::Text("Ammo");
    for (uint32_t ammoIndex = 0, drawnAmmoItems = 0; ammoIndex < 16; ammoIndex++) {
        uint8_t item = (restrictToValid) ? gAmmoItems[ammoIndex] : gAllAmmoItems[ammoIndex];
        if (item != ITEM_NONE) {
            // For legal items, display as 1 row of 7. For unrestricted items, display rows of 6 to match
            // inventory
            if ((restrictToValid && (drawnAmmoItems != 0)) || ((drawnAmmoItems % 6) != 0)) {
                ImGui::SameLine();
            }
            drawnAmmoItems++;

            ImGui::PushID(ammoIndex);
            ImGui::PushItemWidth(IMAGE_SIZE);
            ImGui::BeginGroup();

            ImGui::Image(
                std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                    ->GetTextureByName(itemMapping[item].name),
                ImVec2(IMAGE_SIZE, IMAGE_SIZE));
            PushStyleInput(THEME_COLOR);
            ImGui::InputScalar("##ammoInput", ImGuiDataType_S8, &AMMO(item));
            PopStyleInput();

            ImGui::EndGroup();
            ImGui::PopItemWidth();
            ImGui::PopID();
        }
    }

    // Trade quest flags are only used when shuffling the trade sequence, so
    // don't show this if it isn't needed.
    if (IS_RANDO && OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_SHUFFLE_ADULT_TRADE) &&
        ImGui::TreeNode("Adult trade quest items")) {
        for (int i = ITEM_POCKET_EGG; i <= ITEM_CLAIM_CHECK; i++) {
            DrawBGSItemFlag(i);
        }
        ImGui::TreePop();
    }
}

// Draw a flag bitfield as an grid of checkboxes
