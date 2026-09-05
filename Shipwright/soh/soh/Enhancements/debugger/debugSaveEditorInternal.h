#pragma once

#include "soh/SohGui/UIWidgets.hpp"

#include <imgui.h>

#include <string>

extern UIWidgets::IntSliderOptions intSliderOptionsBase;
extern UIWidgets::ButtonOptions buttonOptionsBase;
extern UIWidgets::CheckboxOptions checkboxOptionsBase;
extern UIWidgets::ComboboxOptions comboboxOptionsBase;

inline constexpr float IMAGE_SIZE = 48.0f;

extern u8 gAllAmmoItems[];

// Encapsulates one save-editor control group and its border.
template <typename DrawFunction> void DrawSaveEditorGroup(DrawFunction&& drawFunction, const std::string& section) {
    ImGui::BeginChild(("##" + section).c_str(), ImVec2(0, 0),
                      ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeX |
                          ImGuiChildFlags_AutoResizeY);
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    drawFunction();
    ImGui::EndGroup();
    ImGui::EndChild();
}

void DrawInfoTab();
void DrawInventoryTab();
void DrawPlayerTab();
