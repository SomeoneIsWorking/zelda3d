#include "randomizer_item_tracker.h"

#include <memory>

#include <libultraship/controller/controldeck/ControlDeck.h>

#include "randomizer_item_tracker_layout.h"
#include "randomizer_item_tracker_model.h"
#include "randomizer_item_tracker_persistence.h"
#include "randomizer_item_tracker_widgets.h"
#include "soh/OTRGlobals.h"
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/UIWidgets.hpp"

#include <fast/Fast3dGui.h>

extern "C" {
#include <z64.h>
#include "variables.h"
extern PlayState* gPlayState;
}

using namespace UIWidgets;

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
}

namespace {
WidgetInfo backgroundColor;
WidgetInfo windowTypeWidget;
WidgetInfo ammoTracking;
WidgetInfo keyTracking;
WidgetInfo triforcePieceCount;
WidgetInfo dungeonItemTracking;
WidgetInfo gregTracking;
WidgetInfo triforcePieceTracking;
WidgetInfo beanSoulsTracking;
WidgetInfo bossSoulsTracking;
WidgetInfo jabberNutsTracking;
WidgetInfo ocarinaButtonTracking;
WidgetInfo overworldKeysTracking;
WidgetInfo fishingPoleTracking;
WidgetInfo personalNotesWidget;
WidgetInfo hookshotIdentWidget;
} // namespace

void ItemTrackerWindow::Draw() {
    if (!IsVisible()) {
        return;
    }
    ImGui::PushFont(OTRGlobals::Instance->fontMono);
    DrawElement();
    // Sync up the IsVisible flag if it was changed by ImGui
    SyncVisibilityConsoleVariable();
    ImGui::PopFont();
}

void ItemTrackerWindow::DrawElement() {
    UpdateVectors();

    int iconSize = CVarGetInteger(CVAR_TRACKER_ITEM("IconSize"), 36);
    int iconSpacing = CVarGetInteger(CVAR_TRACKER_ITEM("IconSpacing"), 12);
    int comboButton1Mask = buttonMap[CVarGetInteger(CVAR_TRACKER_ITEM("ComboButton1"), TRACKER_COMBO_BUTTON_L)];
    int comboButton2Mask = buttonMap[CVarGetInteger(CVAR_TRACKER_ITEM("ComboButton2"), TRACKER_COMBO_BUTTON_R)];
    OSContPad* buttonsPressed =
        std::dynamic_pointer_cast<LUS::ControlDeck>(Ship::Context::GetRawInstance()->GetControlDeck())->GetPads();
    bool comboButtonsHeld = buttonsPressed != nullptr && buttonsPressed[0].button & comboButton1Mask &&
                            buttonsPressed[0].button & comboButton2Mask;
    bool isPaused = CVarGetInteger(CVAR_TRACKER_ITEM("ShowOnlyPaused"), 0) == 0 ||
                    gPlayState != nullptr && gPlayState->pauseCtx.state > 0;

    if (CVarGetInteger(CVAR_TRACKER_ITEM("WindowType"), TRACKER_WINDOW_FLOATING) == TRACKER_WINDOW_WINDOW ||
        isPaused &&
            (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Main"), TRACKER_DISPLAY_ALWAYS) == TRACKER_DISPLAY_ALWAYS
                 ? CVarGetInteger(CVAR_WINDOW("ItemTracker"), 0)
                 : comboButtonsHeld)) {
        if ((CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Inventory"), SECTION_DISPLAY_MAIN_WINDOW) ==
             SECTION_DISPLAY_MAIN_WINDOW) ||
            (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Equipment"), SECTION_DISPLAY_MAIN_WINDOW) ==
             SECTION_DISPLAY_MAIN_WINDOW) ||
            (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Misc"), SECTION_DISPLAY_MAIN_WINDOW) ==
             SECTION_DISPLAY_MAIN_WINDOW) ||
            (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.DungeonRewards"), SECTION_DISPLAY_MAIN_WINDOW) ==
             SECTION_DISPLAY_MAIN_WINDOW) ||
            (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Songs"), SECTION_DISPLAY_MAIN_WINDOW) ==
             SECTION_DISPLAY_MAIN_WINDOW) ||
            (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.DungeonItems"), SECTION_DISPLAY_HIDDEN) ==
             SECTION_DISPLAY_MAIN_WINDOW) ||
            (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Greg"), SECTION_DISPLAY_EXTENDED_HIDDEN) ==
             SECTION_DISPLAY_EXTENDED_MAIN_WINDOW) ||
            (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.TriforcePieces"), SECTION_DISPLAY_HIDDEN) ==
             SECTION_DISPLAY_MAIN_WINDOW) ||
            (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.FishingPole"), SECTION_DISPLAY_EXTENDED_HIDDEN) ==
             SECTION_DISPLAY_EXTENDED_MAIN_WINDOW) ||
            (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Notes"), SECTION_DISPLAY_HIDDEN) ==
             SECTION_DISPLAY_MAIN_WINDOW)) {
            BeginFloatingWindows("Item Tracker");
            DrawItemsInRows(mainWindowItems, 6);

            if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Notes"), SECTION_DISPLAY_HIDDEN) ==
                SECTION_DISPLAY_MAIN_WINDOW) {
                DrawItemTrackerNotes();
            }
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Inventory"), SECTION_DISPLAY_MAIN_WINDOW) ==
            SECTION_DISPLAY_SEPARATE) {
            BeginFloatingWindows("Inventory Items Tracker");
            DrawItemsInRows(inventoryItems);
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Equipment"), SECTION_DISPLAY_MAIN_WINDOW) ==
            SECTION_DISPLAY_SEPARATE) {
            BeginFloatingWindows("Equipment Items Tracker");
            DrawItemsInRows(equipmentItems, 3);
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Misc"), SECTION_DISPLAY_MAIN_WINDOW) ==
            SECTION_DISPLAY_SEPARATE) {
            BeginFloatingWindows("Misc Items Tracker");
            DrawItemsInRows(miscItems, 4);
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.DungeonRewards"), SECTION_DISPLAY_MAIN_WINDOW) ==
            SECTION_DISPLAY_SEPARATE) {
            BeginFloatingWindows("Dungeon Rewards Tracker");
            if (CVarGetInteger(CVAR_TRACKER_ITEM("DungeonRewardsLayout"), 0)) {
                ImGui::BeginGroup();
                DrawItemsInACircle(dungeonRewardMedallions);
                ImGui::EndGroup();
                ImGui::BeginGroup();
                DrawItemsInRows(dungeonRewardStones);
                ImGui::EndGroup();
            } else {
                DrawItemsInRows(dungeonRewards, 3);
            }
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Songs"), SECTION_DISPLAY_MAIN_WINDOW) ==
            SECTION_DISPLAY_SEPARATE) {
            BeginFloatingWindows("Songs Tracker");
            DrawItemsInRows(songItems);
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.DungeonItems"), SECTION_DISPLAY_HIDDEN) ==
            SECTION_DISPLAY_SEPARATE) {
            BeginFloatingWindows("Dungeon Items Tracker");
            if (CVarGetInteger(CVAR_TRACKER_ITEM("DungeonItems.Layout"), 1)) {
                if (CVarGetInteger(CVAR_TRACKER_ITEM("DungeonItems.DisplayMaps"), 1)) {
                    DrawItemsInRows(dungeonItems, 12);
                } else {
                    DrawItemsInRows(dungeonItems, 8);
                }
            } else {
                DrawItemsInRows(dungeonItems);
            }
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Greg"), SECTION_DISPLAY_EXTENDED_HIDDEN) ==
            SECTION_DISPLAY_EXTENDED_SEPARATE) {
            BeginFloatingWindows("Greg Tracker");
            DrawItemsInRows(gregItems);
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.TriforcePieces"), SECTION_DISPLAY_HIDDEN) ==
            SECTION_DISPLAY_SEPARATE) {
            BeginFloatingWindows("Triforce Piece Tracker");
            DrawItemsInRows(triforcePieces);
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.BeanSouls"), SECTION_DISPLAY_HIDDEN) ==
            SECTION_DISPLAY_SEPARATE) {
            BeginFloatingWindows("Bean Soul Tracker");
            DrawItemsInRows(beanSoulItems);
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.BossSouls"), SECTION_DISPLAY_HIDDEN) ==
            SECTION_DISPLAY_SEPARATE) {
            BeginFloatingWindows("Boss Soul Tracker");
            DrawItemsInRows(bossSoulItems);
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.JabberNuts"), SECTION_DISPLAY_HIDDEN) ==
            SECTION_DISPLAY_SEPARATE) {
            BeginFloatingWindows("Jabber Nut Tracker");
            DrawItemsInRows(jabbernutItems);
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.OcarinaButtons"), SECTION_DISPLAY_HIDDEN) ==
            SECTION_DISPLAY_SEPARATE) {
            BeginFloatingWindows("Ocarina Button Tracker");
            DrawItemsInRows(ocarinaButtonItems);
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.OverworldKeys"), SECTION_DISPLAY_HIDDEN) ==
            SECTION_DISPLAY_SEPARATE) {
            BeginFloatingWindows("Overworld Key Tracker");
            DrawItemsInRows(overworldKeyItems);
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.FishingPole"), SECTION_DISPLAY_EXTENDED_HIDDEN) ==
            SECTION_DISPLAY_EXTENDED_SEPARATE) {
            BeginFloatingWindows("Fishing Pole Tracker");
            DrawItemsInRows(fishingPoleItems);
            EndFloatingWindows();
        }

        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Notes"), SECTION_DISPLAY_HIDDEN) ==
                SECTION_DISPLAY_SEPARATE &&
            (CVarGetInteger(CVAR_TRACKER_ITEM("WindowType"), TRACKER_WINDOW_FLOATING) == TRACKER_WINDOW_WINDOW ||
             (CVarGetInteger(CVAR_TRACKER_ITEM("WindowType"), TRACKER_WINDOW_FLOATING) == TRACKER_WINDOW_FLOATING &&
              CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Main"), TRACKER_DISPLAY_ALWAYS) !=
                  TRACKER_DISPLAY_COMBO_BUTTON))) {
            ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
            BeginFloatingWindows("Personal Notes", ImGuiWindowFlags_NoFocusOnAppearing);
            DrawItemTrackerNotes(true);
            EndFloatingWindows();
        }

        if (CVarGetInteger("gTrackers.ItemTracker.TotalChecks.DisplayType", SECTION_DISPLAY_MINIMAL_HIDDEN) ==
            SECTION_DISPLAY_MINIMAL_SEPARATE) {
            ImGui::SetNextWindowSize(ImVec2(450, 300), ImGuiCond_FirstUseEver);
            BeginFloatingWindows("Total Checks");
            DrawTotalChecks();
            EndFloatingWindows();
        }
    }
    FinishItemTrackerPresetPlacement();
}

static std::map<int32_t, const char*> itemTrackerCapacityTrackOptions = {
    { ITEM_TRACKER_NUMBER_NONE, "No Numbers" },
    { ITEM_TRACKER_NUMBER_CURRENT_CAPACITY_ONLY, "Current Capacity" },
    { ITEM_TRACKER_NUMBER_CURRENT_AMMO_ONLY, "Current Ammo" },
    { ITEM_TRACKER_NUMBER_CAPACITY, "Current Capacity / Max Capacity" },
    { ITEM_TRACKER_NUMBER_AMMO, "Current Ammo / Current Capacity" },
};
static std::map<int32_t, const char*> itemTrackerKeyTrackOptions = {
    { KEYS_COLLECTED_MAX, "Collected / Max" },
    { KEYS_CURRENT_COLLECTED_MAX, "Current / Collected / Max" },
    { KEYS_CURRENT_MAX, "Current / Max" },
};
static std::map<int32_t, const char*> itemTrackerTriforcePieceTrackOptions = {
    { TRIFORCE_PIECE_COLLECTED_REQUIRED, "Collected / Required" },
    { TRIFORCE_PIECE_COLLECTED_REQUIRED_MAX, "Collected / Required / Max" },
};
static std::map<int32_t, const char*> displayTypes = {
    { SECTION_DISPLAY_HIDDEN, "Hidden" },
    { SECTION_DISPLAY_MAIN_WINDOW, "Main Window" },
    { SECTION_DISPLAY_SEPARATE, "Separate" },
};
static std::map<int32_t, const char*> extendedDisplayTypes = {
    { SECTION_DISPLAY_EXTENDED_HIDDEN, "Hidden" },
    { SECTION_DISPLAY_EXTENDED_MAIN_WINDOW, "Main Window" },
    { SECTION_DISPLAY_EXTENDED_MISC_WINDOW, "Misc Window" },
    { SECTION_DISPLAY_EXTENDED_SEPARATE, "Separate" },
};
static std::map<int32_t, const char*> minimalDisplayTypes = { { SECTION_DISPLAY_MINIMAL_HIDDEN, "Hidden" },
                                                              { SECTION_DISPLAY_MINIMAL_SEPARATE, "Separate" } };

void ItemTrackerSettingsWindow::DrawElement() {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, { 8.0f, 8.0f });
    if (ImGui::BeginTable("itemTrackerSettingsTable", 2, ImGuiTableFlags_BordersH | ImGuiTableFlags_BordersV)) {
        ImGui::TableSetupColumn("General settings", ImGuiTableColumnFlags_WidthStretch, 200.0f);
        ImGui::TableSetupColumn("Section settings", ImGuiTableColumnFlags_WidthStretch, 200.0f);
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        SohGui::mSohMenu->MenuDrawItem(backgroundColor, 250, THEME_COLOR);
        ImGui::PopItemWidth();
        SohGui::mSohMenu->MenuDrawItem(windowTypeWidget, 250, THEME_COLOR);

        if (CVarGetInteger(CVAR_TRACKER_ITEM("WindowType"), TRACKER_WINDOW_FLOATING) == TRACKER_WINDOW_FLOATING) {
            if (CVarCheckbox("Enable Dragging", CVAR_TRACKER_ITEM("Draggable"), CheckboxOptions().Color(THEME_COLOR))) {
                shouldUpdateVectors = true;
            }
            if (CVarCheckbox("Only Enable While Paused", CVAR_TRACKER_ITEM("ShowOnlyPaused"),
                             CheckboxOptions().Color(THEME_COLOR))) {
                shouldUpdateVectors = true;
            }
            if (CVarCombobox("Display Mode", CVAR_TRACKER_ITEM("DisplayType.Main"), showMode,
                             ComboboxOptions()
                                 .DefaultIndex(TRACKER_DISPLAY_ALWAYS)
                                 .ComponentAlignment(ComponentAlignments::Right)
                                 .LabelPosition(LabelPositions::Far)
                                 .Color(THEME_COLOR))) {
                shouldUpdateVectors = true;
            }
            if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.Main"), TRACKER_DISPLAY_ALWAYS) ==
                TRACKER_DISPLAY_COMBO_BUTTON) {
                if (CVarCombobox("Combo Button 1", CVAR_TRACKER_ITEM("ComboButton1"), buttonStrings,
                                 ComboboxOptions()
                                     .DefaultIndex(TRACKER_COMBO_BUTTON_L)
                                     .ComponentAlignment(ComponentAlignments::Right)
                                     .LabelPosition(LabelPositions::Far)
                                     .Color(THEME_COLOR))) {
                    shouldUpdateVectors = true;
                }
                if (CVarCombobox("Combo Button 2", CVAR_TRACKER_ITEM("ComboButton2"), buttonStrings,
                                 ComboboxOptions()
                                     .DefaultIndex(TRACKER_COMBO_BUTTON_R)
                                     .ComponentAlignment(ComponentAlignments::Right)
                                     .LabelPosition(LabelPositions::Far)
                                     .Color(THEME_COLOR))) {
                    shouldUpdateVectors = true;
                }
            }
        }
        ImGui::Separator();
        CVarSliderInt("Icon size : %dpx", CVAR_TRACKER_ITEM("IconSize"),
                      IntSliderOptions().Min(25).Max(128).DefaultValue(36).Color(THEME_COLOR));
        CVarSliderInt("Icon margins : %dpx", CVAR_TRACKER_ITEM("IconSpacing"),
                      IntSliderOptions().Min(-5).Max(50).DefaultValue(12).Color(THEME_COLOR));
        CVarSliderInt("Text size : %dpx", CVAR_TRACKER_ITEM("TextSize"),
                      IntSliderOptions().Min(1).Max(30).DefaultValue(13).Color(THEME_COLOR));

        ImGui::NewLine();
        SohGui::mSohMenu->MenuDrawItem(ammoTracking, 250, THEME_COLOR);
        if (CVarGetInteger(CVAR_TRACKER_ITEM("ItemCountType"), ITEM_TRACKER_NUMBER_CURRENT_CAPACITY_ONLY) ==
                ITEM_TRACKER_NUMBER_CURRENT_CAPACITY_ONLY ||
            CVarGetInteger(CVAR_TRACKER_ITEM("ItemCountType"), ITEM_TRACKER_NUMBER_CURRENT_CAPACITY_ONLY) ==
                ITEM_TRACKER_NUMBER_CURRENT_AMMO_ONLY) {
            if (CVarCheckbox("Align count to left side", CVAR_TRACKER_ITEM("ItemCountAlignLeft"),
                             CheckboxOptions().Color(THEME_COLOR))) {
                shouldUpdateVectors = true;
            }
        }

        SohGui::mSohMenu->MenuDrawItem(keyTracking, 250, THEME_COLOR);
        SohGui::mSohMenu->MenuDrawItem(triforcePieceCount, 250, THEME_COLOR);

        ImGui::TableNextColumn();

        if (CVarCombobox("Inventory", CVAR_TRACKER_ITEM("DisplayType.Inventory"), displayTypes,
                         ComboboxOptions()
                             .DefaultIndex(SECTION_DISPLAY_MAIN_WINDOW)
                             .ComponentAlignment(ComponentAlignments::Right)
                             .LabelPosition(LabelPositions::Far)
                             .Color(THEME_COLOR))) {
            shouldUpdateVectors = true;
        }
        if (CVarCombobox("Equipment", CVAR_TRACKER_ITEM("DisplayType.Equipment"), displayTypes,
                         ComboboxOptions()
                             .DefaultIndex(SECTION_DISPLAY_MAIN_WINDOW)
                             .ComponentAlignment(ComponentAlignments::Right)
                             .LabelPosition(LabelPositions::Far)
                             .Color(THEME_COLOR))) {
            shouldUpdateVectors = true;
        }
        if (CVarCombobox("Misc", CVAR_TRACKER_ITEM("DisplayType.Misc"), displayTypes,
                         ComboboxOptions()
                             .DefaultIndex(SECTION_DISPLAY_MAIN_WINDOW)
                             .ComponentAlignment(ComponentAlignments::Right)
                             .LabelPosition(LabelPositions::Far)
                             .Color(THEME_COLOR))) {
            shouldUpdateVectors = true;
        }
        if (CVarCombobox("Dungeon Rewards", CVAR_TRACKER_ITEM("DisplayType.DungeonRewards"), displayTypes,
                         ComboboxOptions()
                             .DefaultIndex(SECTION_DISPLAY_MAIN_WINDOW)
                             .ComponentAlignment(ComponentAlignments::Right)
                             .LabelPosition(LabelPositions::Far)
                             .Color(THEME_COLOR))) {
            shouldUpdateVectors = true;
        }
        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.DungeonRewards"), SECTION_DISPLAY_MAIN_WINDOW) ==
            SECTION_DISPLAY_SEPARATE) {
            if (CVarCheckbox("Circle display", CVAR_TRACKER_ITEM("DungeonRewardsLayout"),
                             CheckboxOptions().DefaultValue(false).Color(THEME_COLOR))) {
                shouldUpdateVectors = true;
            }
        }
        if (CVarCombobox("Songs", CVAR_TRACKER_ITEM("DisplayType.Songs"), displayTypes,
                         ComboboxOptions()
                             .DefaultIndex(SECTION_DISPLAY_MAIN_WINDOW)
                             .ComponentAlignment(ComponentAlignments::Right)
                             .LabelPosition(LabelPositions::Far)
                             .Color(THEME_COLOR))) {
            shouldUpdateVectors = true;
        }
        SohGui::mSohMenu->MenuDrawItem(dungeonItemTracking, 250, THEME_COLOR);
        if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.DungeonItems"), SECTION_DISPLAY_HIDDEN) !=
            SECTION_DISPLAY_HIDDEN) {
            if (CVarGetInteger(CVAR_TRACKER_ITEM("DisplayType.DungeonItems"), SECTION_DISPLAY_HIDDEN) ==
                SECTION_DISPLAY_SEPARATE) {
                if (CVarCheckbox("Horizontal display", CVAR_TRACKER_ITEM("DungeonItems.Layout"),
                                 CheckboxOptions().DefaultValue(true).Color(THEME_COLOR))) {
                    shouldUpdateVectors = true;
                }
            }
            if (CVarCheckbox("Maps and compasses", CVAR_TRACKER_ITEM("DungeonItems.DisplayMaps"),
                             CheckboxOptions().DefaultValue(true).Color(THEME_COLOR))) {
                shouldUpdateVectors = true;
            }
        }
        SohGui::mSohMenu->MenuDrawItem(gregTracking, 250, THEME_COLOR);
        SohGui::mSohMenu->MenuDrawItem(triforcePieceTracking, 250, THEME_COLOR);
        SohGui::mSohMenu->MenuDrawItem(beanSoulsTracking, 250, THEME_COLOR);
        SohGui::mSohMenu->MenuDrawItem(bossSoulsTracking, 250, THEME_COLOR);
        SohGui::mSohMenu->MenuDrawItem(jabberNutsTracking, 250, THEME_COLOR);
        SohGui::mSohMenu->MenuDrawItem(ocarinaButtonTracking, 250, THEME_COLOR);
        SohGui::mSohMenu->MenuDrawItem(overworldKeysTracking, 250, THEME_COLOR);
        SohGui::mSohMenu->MenuDrawItem(fishingPoleTracking, 250, THEME_COLOR);

        if (CVarCombobox("Total Checks", CVAR_TRACKER_ITEM("TotalChecks.DisplayType"), minimalDisplayTypes,
                         ComboboxOptions()
                             .DefaultIndex(SECTION_DISPLAY_MINIMAL_HIDDEN)
                             .ComponentAlignment(ComponentAlignments::Right)
                             .LabelPosition(LabelPositions::Far)
                             .Color(THEME_COLOR))) {
            shouldUpdateVectors = true;
        }

        SohGui::mSohMenu->MenuDrawItem(personalNotesWidget, 250, THEME_COLOR);
        SohGui::mSohMenu->MenuDrawItem(hookshotIdentWidget, 250, THEME_COLOR);

        ImGui::PopStyleVar(1);
        ImGui::EndTable();
    }
}

void ItemTrackerWindow::InitElement() {
    InitializeItemTrackerPersistence();
}

void RegisterItemTrackerWidgets() {
    backgroundColor = { .name = "Background Color##ItemTracker", .type = WidgetType::WIDGET_CVAR_COLOR_PICKER };
    backgroundColor.CVar(CVAR_TRACKER_ITEM("BgColor"))
        .Options(
            ColorPickerOptions().Color(THEME_COLOR).DefaultValue({ 0, 0, 0, 0 }).UseAlpha().ShowReset().ShowRandom());
    SohGui::mSohMenu->AddSearchWidget({ backgroundColor, "Randomizer", "Item Tracker", "General Settings" });

    windowTypeWidget = { .name = "Window Type##ItemTracker", .type = WidgetType::WIDGET_CVAR_COMBOBOX };
    windowTypeWidget.CVar(CVAR_TRACKER_ITEM("WindowType"))
        .Options(ComboboxOptions()
                     .DefaultIndex(TRACKER_WINDOW_FLOATING)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far)
                     .Color(THEME_COLOR)
                     .ComboMap(windowType))
        .Callback([](WidgetInfo& info) { shouldUpdateVectors = true; });
    SohGui::mSohMenu->AddSearchWidget({ windowTypeWidget, "Randomizer", "Item Tracker", "General Settings" });
    ammoTracking = { .name = "Ammo/Capacity Tracking", .type = WidgetType::WIDGET_CVAR_COMBOBOX };
    ammoTracking.CVar(CVAR_TRACKER_ITEM("ItemCountType"))
        .Options(ComboboxOptions()
                     .DefaultIndex(ITEM_TRACKER_NUMBER_CURRENT_CAPACITY_ONLY)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far)
                     .Color(THEME_COLOR)
                     .ComboMap(itemTrackerCapacityTrackOptions)
                     .Tooltip("Customize what the numbers under each item are tracking."
                              "\n\nNote: items without capacity upgrades will track ammo even in capacity mode"));
    SohGui::mSohMenu->AddSearchWidget({ ammoTracking, "Randomizer", "Item Tracker", "General Settings" });

    keyTracking = { .name = "Key Count Tracking", .type = WidgetType::WIDGET_CVAR_COMBOBOX };
    keyTracking.CVar(CVAR_TRACKER_ITEM("KeyCounts"))
        .Options(ComboboxOptions()
                     .DefaultIndex(KEYS_COLLECTED_MAX)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far)
                     .Color(THEME_COLOR)
                     .ComboMap(itemTrackerKeyTrackOptions)
                     .Tooltip("Customize what numbers are shown for key tracking."));
    SohGui::mSohMenu->AddSearchWidget({ keyTracking, "Randomizer", "Item Tracker", "General Settings" });

    triforcePieceTracking = { .name = "Triforce Pieces", .type = WidgetType::WIDGET_CVAR_COMBOBOX };
    triforcePieceTracking.CVar(CVAR_TRACKER_ITEM("DisplayType.TriforcePieces"))
        .Options(ComboboxOptions()
                     .DefaultIndex(SECTION_DISPLAY_HIDDEN)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far)
                     .Color(THEME_COLOR)
                     .ComboMap(displayTypes))
        .Callback([](WidgetInfo& info) { shouldUpdateVectors = true; });
    SohGui::mSohMenu->AddSearchWidget({ triforcePieceTracking, "Randomizer", "Item Tracker", "General Settings" });

    dungeonItemTracking = { .name = "Dungeon Items", .type = WidgetType::WIDGET_CVAR_COMBOBOX };
    dungeonItemTracking.CVar(CVAR_TRACKER_ITEM("DisplayType.DungeonItems"))
        .Options(ComboboxOptions()
                     .DefaultIndex(SECTION_DISPLAY_HIDDEN)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far)
                     .Color(THEME_COLOR)
                     .ComboMap(displayTypes))
        .Callback([](WidgetInfo& info) { shouldUpdateVectors = true; });
    ;
    SohGui::mSohMenu->AddSearchWidget(
        { dungeonItemTracking, "Randomizer", "Item Tracker", "General Settings", "keys maps compasses icon" });

    gregTracking = { .name = "Greg", .type = WidgetType::WIDGET_CVAR_COMBOBOX };
    gregTracking.CVar(CVAR_TRACKER_ITEM("DisplayType.Greg"))
        .Options(ComboboxOptions()
                     .DefaultIndex(SECTION_DISPLAY_EXTENDED_HIDDEN)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far)
                     .Color(THEME_COLOR)
                     .ComboMap(extendedDisplayTypes))
        .Callback([](WidgetInfo& info) { shouldUpdateVectors = true; });
    ;
    SohGui::mSohMenu->AddSearchWidget({ gregTracking, "Randomizer", "Item Tracker", "General Settings", "icon" });

    beanSoulsTracking = { .name = "Bean Souls", .type = WidgetType::WIDGET_CVAR_COMBOBOX };
    beanSoulsTracking.CVar(CVAR_TRACKER_ITEM("DisplayType.BeanSouls"))
        .Options(ComboboxOptions()
                     .DefaultIndex(SECTION_DISPLAY_HIDDEN)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far)
                     .Color(THEME_COLOR)
                     .ComboMap(displayTypes))
        .Callback([](WidgetInfo& info) { shouldUpdateVectors = true; });
    ;
    SohGui::mSohMenu->AddSearchWidget({ beanSoulsTracking, "Randomizer", "Item Tracker", "General Settings", "icon" });

    bossSoulsTracking = { .name = "Boss Souls", .type = WidgetType::WIDGET_CVAR_COMBOBOX };
    bossSoulsTracking.CVar(CVAR_TRACKER_ITEM("DisplayType.BossSouls"))
        .Options(ComboboxOptions()
                     .DefaultIndex(SECTION_DISPLAY_HIDDEN)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far)
                     .Color(THEME_COLOR)
                     .ComboMap(displayTypes))
        .Callback([](WidgetInfo& info) { shouldUpdateVectors = true; });
    ;
    SohGui::mSohMenu->AddSearchWidget({ bossSoulsTracking, "Randomizer", "Item Tracker", "General Settings", "icon" });

    jabberNutsTracking = { .name = "Jabber Nuts", .type = WidgetType::WIDGET_CVAR_COMBOBOX };
    jabberNutsTracking.CVar(CVAR_TRACKER_ITEM("DisplayType.JabberNuts"))
        .Options(ComboboxOptions()
                     .DefaultIndex(SECTION_DISPLAY_HIDDEN)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far)
                     .Color(THEME_COLOR)
                     .ComboMap(displayTypes))
        .Callback([](WidgetInfo& info) { shouldUpdateVectors = true; });
    ;
    SohGui::mSohMenu->AddSearchWidget({ jabberNutsTracking, "Randomizer", "Item Tracker", "General Settings", "icon" });

    triforcePieceCount = { .name = "Triforce Piece Count Tracking", .type = WidgetType::WIDGET_CVAR_COMBOBOX };
    triforcePieceCount.CVar(CVAR_TRACKER_ITEM("TriforcePieceCounts"))
        .Options(ComboboxOptions()
                     .DefaultIndex(TRIFORCE_PIECE_COLLECTED_REQUIRED_MAX)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far)
                     .Color(THEME_COLOR)
                     .ComboMap(itemTrackerTriforcePieceTrackOptions)
                     .Tooltip("Customize what numbers are shown for triforce piece tracking."));
    SohGui::mSohMenu->AddSearchWidget({ triforcePieceCount, "Randomizer", "Item Tracker", "General Settings" });

    ocarinaButtonTracking = { .name = "Ocarina Buttons", .type = WidgetType::WIDGET_CVAR_COMBOBOX };
    ocarinaButtonTracking.CVar(CVAR_TRACKER_ITEM("DisplayType.OcarinaButtons"))
        .Options(ComboboxOptions()
                     .DefaultIndex(SECTION_DISPLAY_HIDDEN)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far)
                     .Color(THEME_COLOR)
                     .ComboMap(displayTypes))
        .Callback([](WidgetInfo& info) { shouldUpdateVectors = true; });
    ;
    SohGui::mSohMenu->AddSearchWidget(
        { ocarinaButtonTracking, "Randomizer", "Item Tracker", "General Settings", "icon" });

    overworldKeysTracking = { .name = "Overworld Keys", .type = WidgetType::WIDGET_CVAR_COMBOBOX };
    overworldKeysTracking.CVar(CVAR_TRACKER_ITEM("DisplayType.OverworldKeys"))
        .Options(ComboboxOptions()
                     .DefaultIndex(SECTION_DISPLAY_HIDDEN)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far)
                     .Color(THEME_COLOR)
                     .ComboMap(displayTypes))
        .Callback([](WidgetInfo& info) { shouldUpdateVectors = true; });
    ;
    SohGui::mSohMenu->AddSearchWidget(
        { overworldKeysTracking, "Randomizer", "Item Tracker", "General Settings", "icon" });

    fishingPoleTracking = { .name = "Fishing Pole", .type = WidgetType::WIDGET_CVAR_COMBOBOX };
    fishingPoleTracking.CVar(CVAR_TRACKER_ITEM("DisplayType.FishingPole"))
        .Options(ComboboxOptions()
                     .DefaultIndex(SECTION_DISPLAY_EXTENDED_HIDDEN)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far)
                     .Color(THEME_COLOR)
                     .ComboMap(extendedDisplayTypes))
        .Callback([](WidgetInfo& info) { shouldUpdateVectors = true; });
    ;
    SohGui::mSohMenu->AddSearchWidget(
        { fishingPoleTracking, "Randomizer", "Item Tracker", "General Settings", "icon" });

    personalNotesWidget = { .name = "Personal notes", .type = WidgetType::WIDGET_CVAR_COMBOBOX };
    static const char* notesDisabledTooltip =
        "Disabled because tracker is set to floating and display combo is enabled.";
    personalNotesWidget.CVar(CVAR_TRACKER_ITEM("DisplayType.Notes"))
        .Options(ComboboxOptions()
                     .DefaultIndex(SECTION_DISPLAY_HIDDEN)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far)
                     .Color(THEME_COLOR)
                     .ComboMap(displayTypes))
        .Callback([](WidgetInfo& info) { shouldUpdateVectors = true; });
    ;
    SohGui::mSohMenu->AddSearchWidget({ personalNotesWidget, "Randomizer", "Item Tracker", "General Settings" });

    hookshotIdentWidget = { .name = "Show Hookshot Identifiers", .type = WidgetType::WIDGET_CVAR_CHECKBOX };
    hookshotIdentWidget.CVar(CVAR_TRACKER_ITEM("HookshotIdentifier"))
        .Options(CheckboxOptions()
                     .Color(THEME_COLOR)
                     .Tooltip("Shows an 'H' or an 'L' to more easily distinguish between Hookshot and Longshot."));
    SohGui::mSohMenu->AddSearchWidget({ hookshotIdentWidget, "Randomizer", "Item Tracker", "General Settings" });
}

void RegisterItemTracker() {
    COND_HOOK(OnLoadFile, true, [](int32_t fileNum) { shouldUpdateVectors = true; });
}
