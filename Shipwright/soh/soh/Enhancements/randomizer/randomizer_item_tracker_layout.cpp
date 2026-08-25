#include "randomizer_item_tracker_layout.h"

#include <cmath>
#include <cstring>
#include <string>

#include "randomizer_item_tracker_persistence.h"
#include "randomizerEnums.h"
#include "soh/host/math_constants.h"
#include "soh/cvar_prefixes.h"
#include "soh/SohGui/UIWidgets.hpp"

#include <imgui_internal.h>

using namespace UIWidgets;

void BeginFloatingWindows(const char* uniqueName, int flags) {
    ImGuiWindowFlags windowFlags = flags;

    if (windowFlags == 0) {
        windowFlags |=
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoResize;
    }

    if (CVarGetInteger(CVAR_TRACKER_ITEM("WindowType"), TRACKER_WINDOW_FLOATING) == TRACKER_WINDOW_FLOATING) {
        ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
        windowFlags |= ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoTitleBar |
                       ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;

        if (!CVarGetInteger(CVAR_TRACKER_ITEM("Draggable"), 0)) {
            windowFlags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
        }
    }
    auto color = VecFromRGBA8(CVarGetColor(CVAR_TRACKER_ITEM("BgColor.Value"), { 0, 0, 0, 0 }));
    ImGuiWindow* window = ImGui::FindWindowByName(uniqueName);
    if (window != nullptr && window->DockTabIsVisible && window->ParentWindow != nullptr &&
        std::string(window->ParentWindow->Name).compare(0, std::strlen("Main - Deck"), "Main - Deck") == 0) {
        color.w = 1.0f;
    }
    ImGui::PushStyleColor(ImGuiCol_WindowBg, color);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ApplyItemTrackerPresetPlacement(uniqueName);
    ImGui::Begin(uniqueName, nullptr, windowFlags);
}

void EndFloatingWindows() {
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::End();
}

void DrawItemsInRows(const std::vector<ItemTrackerItem>& items, int itemsPerRow) {
    float iconSize = static_cast<float>(CVarGetInteger(CVAR_TRACKER_ITEM("IconSize"), 36));
    int iconSpacing = CVarGetInteger(CVAR_TRACKER_ITEM("IconSpacing"), 12);
    int topPadding =
        (CVarGetInteger(CVAR_TRACKER_ITEM("WindowType"), TRACKER_WINDOW_FLOATING) == TRACKER_WINDOW_WINDOW) ? 20 : 0;

    for (size_t i = 0; i < items.size(); i++) {
        int row = static_cast<int>(i) / itemsPerRow;
        int column = static_cast<int>(i) % itemsPerRow;
        ImGui::SetCursorPos(
            ImVec2((column * (iconSize + iconSpacing) + 8.0f), (row * (iconSize + iconSpacing)) + 8.0f + topPadding));
        items[i].drawFunc(items[i]);
    }
}

void DrawItemsInACircle(const std::vector<ItemTrackerItem>& items) {
    int iconSize = CVarGetInteger(CVAR_TRACKER_ITEM("IconSize"), 36);
    int iconSpacing = CVarGetInteger(CVAR_TRACKER_ITEM("IconSpacing"), 12);

    ImVec2 max = ImGui::GetWindowContentRegionMax();
    float radius = (iconSize + iconSpacing) * 2.0f;

    for (size_t i = 0; i < items.size(); i++) {
        float angle = static_cast<float>(i) / static_cast<float>(items.size()) * 2.0f * M_PIf;
        float x = (radius / 2.0f) * std::cos(angle) + max.x / 2.0f;
        float y = (radius / 2.0f) * std::sin(angle) + max.y / 2.0f;
        ImGui::SetCursorPos(ImVec2(x - (CVarGetInteger(CVAR_TRACKER_ITEM("IconSize"), 36) - 8) / 2.0f, y + 4));
        items[i].drawFunc(items[i]);
    }
}
