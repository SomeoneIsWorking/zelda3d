#include "CosmeticsEditor.h"

#include "CosmeticsCatalog.h"
#include "CosmeticsColorOperations.h"
#include "CosmeticsEditorLayout.h"
#include "CosmeticsHudPlacement.h"
#include "CosmeticsSillyOptions.h"
#include "cosmeticsTypes.h"
#include "soh/Enhancements/enhancementTypes.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/cvar_prefixes.h"

namespace {
auto& cosmeticOptions = CosmeticOptions();
const auto& cosmeticsRandomizerModes = CosmeticsRandomizerModes();
const char* colorSchemes[] = {
    "N64",
    "Gamecube",
};
} // namespace

void CosmeticsEditorWindow::DrawElement() {
    UIWidgets::CVarCombobox("Color Scheme", CVAR_COSMETIC("DefaultColorScheme"), colorSchemes,
                            UIWidgets::ComboboxOptions()
                                .DefaultIndex(COLORSCHEME_N64)
                                .Color(THEME_COLOR)
                                .LabelPosition(UIWidgets::LabelPositions::Near)
                                .ComponentAlignment(UIWidgets::ComponentAlignments::Right));
    UIWidgets::CVarCheckbox("Sync Rainbow colors", CVAR_COSMETIC("RainbowSync"),
                            UIWidgets::CheckboxOptions().Color(THEME_COLOR));
    UIWidgets::CVarSliderFloat("Rainbow Speed", CVAR_COSMETIC("RainbowSpeed"),
                               UIWidgets::FloatSliderOptions()
                                   .Format("%.2f")
                                   .Min(0.01f)
                                   .Max(1.0f)
                                   .DefaultValue(0.6f)
                                   .Step(0.01f)
                                   .Size(ImVec2(300.0f, 0.0f))
                                   .Color(THEME_COLOR));
    UIWidgets::CVarCombobox(
        "Automatically Randomize All Cosmetics", CVAR_COSMETIC("RandomizeCosmeticsGenModes"), cosmeticsRandomizerModes,
        UIWidgets::ComboboxOptions()
            .DefaultIndex(RANDOMIZE_OFF)
            .Color(THEME_COLOR)
            .Tooltip("Set when the cosmetics is automaticly randomized:\n"
                     "- Manual: Manually randomize cosmetics by pressing the 'Randomize all' button\n"
                     "- On New Scene : Randomizes when you enter a new scene.\n"
                     "- On Rando Gen Only: Randomizes only when you generate a new randomizer.\n"
                     "- On File Load: Randomizes on File Load.\n"
                     "- On File Load (Seeded): Randomizes on file load based on the current randomizer seed/file."));
    UIWidgets::CVarCheckbox(
        "Advanced Mode", CVAR_COSMETIC("AdvancedMode"),
        UIWidgets::CheckboxOptions()
            .Color(THEME_COLOR)
            .Tooltip(
                "Some cosmetic options may not apply if you have any mods that provide custom models for the cosmetic "
                "option.\n\n"
                "For example, if you have custom Link model, then the Link's Hair color option will most likely not "
                "apply."));
    if (CVarGetInteger(CVAR_COSMETIC("AdvancedMode"), 0)) {
        if (UIWidgets::Button("Lock All Advanced",
                              UIWidgets::ButtonOptions().Size(ImVec2(250.0f, 0.0f)).Color(THEME_COLOR))) {
            for (auto& [id, cosmeticOption] : cosmeticOptions) {
                if (cosmeticOption.advancedOption) {
                    CVarSetInteger(cosmeticOption.lockedCvar, 1);
                }
            }
        }
        ImGui::SameLine();
        if (UIWidgets::Button("Unlock All Advanced",
                              UIWidgets::ButtonOptions().Size(ImVec2(250.0f, 0.0f)).Color(THEME_COLOR))) {
            for (auto& [id, cosmeticOption] : cosmeticOptions) {
                if (cosmeticOption.advancedOption) {
                    CVarSetInteger(cosmeticOption.lockedCvar, 0);
                }
            }
        }
    }
    ImGui::BeginDisabled(CVarGetInteger(CVAR_SETTING("DisableChanges"), 0));
    if (UIWidgets::Button("Randomize All", UIWidgets::ButtonOptions().Size(ImVec2(250.0f, 0.0f)).Color(THEME_COLOR))) {
        CosmeticsEditor_RandomizeAll();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (UIWidgets::Button("Reset All", UIWidgets::ButtonOptions().Size(ImVec2(250.0f, 0.0f)).Color(THEME_COLOR))) {
        CosmeticsEditor_ResetAll();
    }
    if (UIWidgets::Button("Lock All", UIWidgets::ButtonOptions().Size(ImVec2(250.0f, 0.0f)).Color(THEME_COLOR))) {
        for (auto& [id, cosmeticOption] : cosmeticOptions) {
            if (!cosmeticOption.advancedOption || CVarGetInteger(CVAR_COSMETIC("AdvancedMode"), 0)) {
                CVarSetInteger(cosmeticOption.lockedCvar, 1);
            }
        }
    }
    ImGui::SameLine();
    if (UIWidgets::Button("Unlock All", UIWidgets::ButtonOptions().Size(ImVec2(250.0f, 0.0f)).Color(THEME_COLOR))) {
        for (auto& [id, cosmeticOption] : cosmeticOptions) {
            if (!cosmeticOption.advancedOption || CVarGetInteger(CVAR_COSMETIC("AdvancedMode"), 0)) {
                CVarSetInteger(cosmeticOption.lockedCvar, 0);
            }
        }
    }

    ImGui::BeginDisabled(CVarGetInteger(CVAR_SETTING("DisableChanges"), 0));
    if (UIWidgets::Button("Rainbow All", UIWidgets::ButtonOptions().Size(ImVec2(250.0f, 0.0f)).Color(THEME_COLOR))) {
        for (auto& [id, cosmeticOption] : cosmeticOptions) {
            if (!CVarGetInteger(cosmeticOption.lockedCvar, 0) &&
                (!cosmeticOption.advancedOption || CVarGetInteger(CVAR_COSMETIC("AdvancedMode"), 0))) {
                CVarSetInteger(cosmeticOption.rainbowCvar, 1);
                CVarSetInteger(cosmeticOption.changedCvar, 1);
            }
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (UIWidgets::Button("Un-Rainbow All", UIWidgets::ButtonOptions().Size(ImVec2(250.0f, 0.0f)).Color(THEME_COLOR))) {
        for (auto& [id, cosmeticOption] : cosmeticOptions) {
            if (!CVarGetInteger(cosmeticOption.lockedCvar, 0) &&
                (!cosmeticOption.advancedOption || CVarGetInteger(CVAR_COSMETIC("AdvancedMode"), 0))) {
                CVarSetInteger(cosmeticOption.rainbowCvar, 0);
            }
        }
    }

    UIWidgets::Spacer(3.0f);

    UIWidgets::PushStyleTabs(THEME_COLOR);
    if (ImGui::BeginTabBar("CosmeticsContextTabBar", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)) {
        if (ImGui::BeginTabItem("Link & Items")) {

            UIWidgets::Separator(true, true, 2.0f, 2.0f);

            for (CosmeticGroup group : CosmeticsEditorLayout::LinkAndItemsGroups()) {
                DrawCosmeticGroup(group);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Keys")) {

            ImGui::BeginDisabled(CVarGetInteger(CVAR_SETTING("DisableChanges"), 0));
            UIWidgets::Separator(true, true, 2.0f, 2.0f);

            if (UIWidgets::Button("Give all keys dungeon-specific colors",
                                  UIWidgets::ButtonOptions().Color(THEME_COLOR).Size(UIWidgets::Sizes::Inline))) {
                ApplyDungeonKeyColors();
            }

            UIWidgets::Separator(true, true, 2.0f, 2.0f);

            for (CosmeticGroup group : CosmeticsEditorLayout::KeyGroups()) {
                DrawCosmeticGroup(group);
            }

            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Effects")) {

            UIWidgets::Separator(true, true, 2.0f, 2.0f);

            for (CosmeticGroup group : CosmeticsEditorLayout::EffectGroups()) {
                DrawCosmeticGroup(group);
            }
            if (UIWidgets::CVarSliderInt("Trails Duration: %d", CVAR_COSMETIC("Trails.Duration.Value"),
                                         UIWidgets::IntSliderOptions()
                                             .Min(2)
                                             .Max(20)
                                             .DefaultValue(4)
                                             .Size(ImVec2(300.0f, 0.0f))
                                             .Color(THEME_COLOR))) {
                CVarSetInteger(CVAR_COSMETIC("Trails.Duration.Changed"), 1);
            }
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::CalcTextSize("g").y * 2));
            if (UIWidgets::Button("Reset##Trails_Duration",
                                  UIWidgets::ButtonOptions().Size(ImVec2(80, 36)).Padding(ImVec2(5.0f, 0.0f)))) {
                CVarClear(CVAR_COSMETIC("Trails.Duration.Value"));
                CVarClear(CVAR_COSMETIC("Trails.Duration.Changed"));
            }

            UIWidgets::Separator(true, true, 2.0f, 2.0f);

            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("World & NPCs")) {

            UIWidgets::Separator(true, true, 2.0f, 2.0f);

            for (CosmeticGroup group : CosmeticsEditorLayout::WorldAndNpcGroups()) {
                DrawCosmeticGroup(group);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Silly")) {
            DrawSillyTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("HUD")) {

            UIWidgets::Separator(true, true, 2.0f, 2.0f);

            for (CosmeticGroup group : CosmeticsEditorLayout::HudGroups()) {
                DrawCosmeticGroup(group);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("HUD Placement")) {
            Draw_Placements();
            ImGui::EndTabItem();
        }

        if (CVarGetInteger(CVAR_COSMETIC("AdvancedMode"), 0)) {
            if (ImGui::BeginTabItem("Pause Menu")) {
                UIWidgets::Separator(true, true, 2.0f, 2.0f);
                DrawCosmeticGroup(COSMETICS_GROUP_KALEIDO);
                ImGui::EndTabItem();
            }
        }

        if (CVarGetInteger(CVAR_COSMETIC("AdvancedMode"), 0)) {
            if (ImGui::BeginTabItem("Message")) {
                UIWidgets::Separator(true, true, 2.0f, 2.0f);
                DrawCosmeticGroup(COSMETICS_GROUP_MESSAGE);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    UIWidgets::PopStyleTabs();
}
