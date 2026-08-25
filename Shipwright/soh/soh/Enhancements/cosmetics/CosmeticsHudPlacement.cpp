#include "CosmeticsHudPlacement.h"

#include "CosmeticsEditor.h"
#include "cosmeticsTypes.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/host/controller_buttons.h"

namespace {
constexpr float kTableCellWidth = 300.0f;
constexpr ImGuiTableColumnFlags kTableFlags = ImGuiTableFlags_BordersH | ImGuiTableFlags_BordersV;
constexpr ImGuiTableColumnFlags kCellFlags =
    ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentEnable | ImGuiTableColumnFlags_NoSort;
} // namespace

static const char* MarginCvarList[]{
    CVAR_COSMETIC("HUD.Hearts"),        CVAR_COSMETIC("HUD.HeartsCount"),    CVAR_COSMETIC("HUD.MagicBar"),
    CVAR_COSMETIC("HUD.VisualSoA"),     CVAR_COSMETIC("HUD.BButton"),        CVAR_COSMETIC("HUD.AButton"),
    CVAR_COSMETIC("HUD.StartButton"),   CVAR_COSMETIC("HUD.CUpButton"),      CVAR_COSMETIC("HUD.CDownButton"),
    CVAR_COSMETIC("HUD.CLeftButton"),   CVAR_COSMETIC("HUD.CRightButton"),   CVAR_COSMETIC("HUD.Dpad"),
    CVAR_COSMETIC("HUD.Minimap"),       CVAR_COSMETIC("HUD.SmallKey"),       CVAR_COSMETIC("HUD.Rupees"),
    CVAR_COSMETIC("HUD.Carrots"),       CVAR_COSMETIC("HUD.Timers"),         CVAR_COSMETIC("HUD.ArcheryScore"),
    CVAR_COSMETIC("HUD.TitleCard.Map"), CVAR_COSMETIC("HUD.TitleCard.Boss"), CVAR_COSMETIC("HUD.IGT"),
};

static const char* MarginCvarNonAnchor[]{
    CVAR_COSMETIC("HUD.Carrots"),       CVAR_COSMETIC("HUD.Timers"),         CVAR_COSMETIC("HUD.ArcheryScore"),
    CVAR_COSMETIC("HUD.TitleCard.Map"), CVAR_COSMETIC("HUD.TitleCard.Boss"),
};

void SetMarginAll(const char* ButtonName, bool SetActivated, const char* tooltip) {
    if (UIWidgets::Button(ButtonName,
                          UIWidgets::ButtonOptions().Size(ImVec2(200.0f, 0.0f)).Color(THEME_COLOR).Tooltip(tooltip))) {
        // MarginCvarNonAnchor is an array that list every element that has No anchor by default, because if that the
        // case this function will not touch it with pose type 0.
        u8 arrayLengthNonMargin = sizeof(MarginCvarNonAnchor) / sizeof(*MarginCvarNonAnchor);
        for (auto cvarName : MarginCvarList) {
            std::string cvarPosType = std::string(cvarName).append(".PosType");
            std::string cvarNameMargins = std::string(cvarName).append(".UseMargins");
            if (CVarGetInteger(cvarPosType.c_str(), 0) <= ANCHOR_RIGHT &&
                SetActivated) { // Our element is not Hidden or Non anchor
                for (int i = 0; i < arrayLengthNonMargin; i++) {
                    if ((strcmp(cvarName, MarginCvarNonAnchor[i]) == 0) &&
                        (CVarGetInteger(cvarPosType.c_str(), 0) ==
                         ORIGINAL_LOCATION)) { // Our element is both in original position and do not have anchor by
                                               // default so we skip it.
                        CVarSetInteger(cvarNameMargins.c_str(), false); // force set off
                    } else if ((strcmp(cvarName, MarginCvarNonAnchor[i]) == 0) &&
                               (CVarGetInteger(cvarPosType.c_str(), 0) !=
                                ORIGINAL_LOCATION)) { // Our element is not in original position regarless it has no
                                                      // anchor by default since player made it anchored we can toggle
                                                      // margins
                        CVarSetInteger(cvarNameMargins.c_str(), SetActivated);
                    } else if (strcmp(cvarName, MarginCvarNonAnchor[i]) !=
                               0) { // Our elements has an anchor by default so regarless of it's position right now
                                    // that okay to toggle margins.
                        CVarSetInteger(cvarNameMargins.c_str(), SetActivated);
                    }
                }
            } else { // Since the user requested to turn all margin off no need to do any check there.
                CVarSetInteger(cvarNameMargins.c_str(), SetActivated);
            }
        }
    }
}

void ResetPositionAll() {
    if (UIWidgets::Button("Reset all positions",
                          UIWidgets::ButtonOptions()
                              .Size(ImVec2(200.0f, 0.0f))
                              .Color(THEME_COLOR)
                              .Tooltip("Revert every element to use their original position and no margins"))) {
        for (auto cvarName : MarginCvarList) {
            std::string cvarPosType = std::string(cvarName).append(".PosType");
            std::string cvarNameMargins = std::string(cvarName).append(".UseMargins");
            CVarSetInteger(cvarPosType.c_str(), 0);
            CVarSetInteger(cvarNameMargins.c_str(), false); // Turn margin off to everythings as that original position.
        }
    }
}

void Table_InitHeader(bool has_header = true) {
    if (has_header) {
        ImGui::TableHeadersRow();
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding(); // This is to adjust Vertical pos of item in a cell to be normlized.
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 2);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 60);
}

void DrawUseMarginsSlider(const std::string ElementName, const std::string CvarName) {
    std::string CvarLabel = CvarName + ".UseMargins";
    std::string Label = ElementName + " use margins";
    UIWidgets::CVarCheckbox(Label.c_str(), CvarLabel.c_str(),
                            UIWidgets::CheckboxOptions()
                                .Color(THEME_COLOR)
                                .Tooltip("Using this allow you move the element with General margins sliders"));
}

void DrawPositionsRadioBoxes(const std::string CvarName, bool NoAnchorEnabled = true) {
    std::string CvarLabel = CvarName + ".PosType";
    UIWidgets::CVarRadioButton("Original position", CvarLabel.c_str(), 0,
                               UIWidgets::RadioButtonsOptions()
                                   .Color(THEME_COLOR)
                                   .Tooltip("This will use original intended elements position"));
    UIWidgets::CVarRadioButton("Anchor to the left", CvarLabel.c_str(), 1,
                               UIWidgets::RadioButtonsOptions()
                                   .Color(THEME_COLOR)
                                   .Tooltip("This will make your elements follow the left side of your game window"));
    UIWidgets::CVarRadioButton("Anchor to the right", CvarLabel.c_str(), 2,
                               UIWidgets::RadioButtonsOptions()
                                   .Color(THEME_COLOR)
                                   .Tooltip("This will make your elements follow the right side of your game window"));
    if (NoAnchorEnabled) {
        UIWidgets::CVarRadioButton(
            "No anchors", CvarLabel.c_str(), 3,
            UIWidgets::RadioButtonsOptions()
                .Color(THEME_COLOR)
                .Tooltip("This will make your elements to not follow any side\nBetter used for center elements"));
    }
    UIWidgets::CVarRadioButton(
        "Hidden", CvarLabel.c_str(), 4,
        UIWidgets::RadioButtonsOptions().Color(THEME_COLOR).Tooltip("This will make your elements hidden"));
}

void DrawPositionSlider(const std::string CvarName, int MinY, int MaxY, int MinX, int MaxX) {
    std::string PosXCvar = CvarName + ".PosX";
    std::string PosYCvar = CvarName + ".PosY";
    std::string InvisibleLabelX = "##" + PosXCvar;
    std::string InvisibleLabelY = "##" + PosYCvar;
    UIWidgets::CVarSliderInt("Up <-> Down : %d", PosYCvar.c_str(),
                             UIWidgets::IntSliderOptions()
                                 .Min(MinY)
                                 .Max(MaxY)
                                 .DefaultValue(0)
                                 .Size(ImVec2(300.0f, 0.0f))
                                 .Color(THEME_COLOR)
                                 .Tooltip("This slider is used to move Up and Down your elements."));
    UIWidgets::CVarSliderInt("Left <-> Right : %d", PosXCvar.c_str(),
                             UIWidgets::IntSliderOptions()
                                 .Min(MinX)
                                 .Max(MaxX)
                                 .DefaultValue(0)
                                 .Size(ImVec2(300.0f, 0.0f))
                                 .Color(THEME_COLOR)
                                 .Tooltip("This slider is used to move Left and Right your elements."));
}

void DrawScaleSlider(const std::string CvarName, float DefaultValue) {
    std::string InvisibleLabel = "##" + CvarName;
    std::string CvarLabel = CvarName + ".Scale";
    // Disabled for now. feature not done and several fixes needed to be merged.
    // UIWidgets::EnhancementSliderFloat("Scale : %dx", InvisibleLabel.c_str(), CvarLabel.c_str(),
    // 0.1f, 3.0f,"",DefaultValue,true);
}

void Draw_Table_Dropdown(const char* Header_Title, const char* Table_ID, const char* Column_Title,
                         const char* Slider_Title, const char* Slider_ID, int MinY, int MaxY, int MinX, int MaxX,
                         float Default_Value) {
    UIWidgets::PushStyleHeader(THEME_COLOR);
    if (ImGui::CollapsingHeader(Header_Title)) {
        if (ImGui::BeginTable(Table_ID, 1, kTableFlags)) {
            ImGui::TableSetupColumn(Column_Title, kCellFlags, kTableCellWidth);
            Table_InitHeader(false);
            DrawUseMarginsSlider(Slider_Title, Slider_ID);
            DrawPositionsRadioBoxes(Slider_ID);
            DrawPositionSlider(Slider_ID, MinY, MaxY, MinX, MaxX);
            DrawScaleSlider(Slider_ID, Default_Value);
            ImGui::EndTable();
        }
    }
    UIWidgets::PopStyleHeader();
}

void C_Button_Dropdown(const char* Header_Title, const char* Table_ID, const char* Column_Title,
                       const char* Slider_Title, const char* Slider_ID, const char* Int_Type,
                       float Slider_Scale_Value) {
    UIWidgets::PushStyleHeader(THEME_COLOR);
    if (ImGui::CollapsingHeader(Header_Title)) {
        if (ImGui::BeginTable(Table_ID, 1, kTableFlags)) {
            ImGui::TableSetupColumn(Column_Title, kCellFlags, kTableCellWidth);
            Table_InitHeader(false);
            DrawUseMarginsSlider(Slider_Title, Slider_ID);
            DrawPositionsRadioBoxes(Slider_ID);
            s16 Min_X_CU = 0;
            s16 Max_X_CU = static_cast<s16>(ImGui::GetWindowViewport()->Size.x / 2);
            if (CVarGetInteger(Int_Type, 0) == 2) {
                Max_X_CU = 294;
            } else if (CVarGetInteger(Int_Type, 0) == 3) {
                Max_X_CU = static_cast<s16>(ImGui::GetWindowViewport()->Size.x / 2);
            } else if (CVarGetInteger(Int_Type, 0) == 4) {
                Min_X_CU = static_cast<s16>(ImGui::GetWindowViewport()->Size.x / 2) * -1;
            }
            DrawPositionSlider(Slider_ID, 0, static_cast<s16>(ImGui::GetWindowViewport()->Size.y / 2), Min_X_CU,
                               Max_X_CU);
            DrawScaleSlider(Slider_ID, Slider_Scale_Value);
            ImGui::EndTable();
        }
        std::shared_ptr<Ship::Controller> controller =
            Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0);
        for (auto [id, mapping] : controller->GetButton(BTN_DDOWN)->GetAllButtonMappings()) {
            controller->GetButton(BTN_CUSTOM_OCARINA_NOTE_F4)->AddButtonMapping(mapping);
        }
        for (auto [id, mapping] : controller->GetButton(BTN_DRIGHT)->GetAllButtonMappings()) {
            controller->GetButton(BTN_CUSTOM_OCARINA_NOTE_A4)->AddButtonMapping(mapping);
        }
        for (auto [id, mapping] : controller->GetButton(BTN_DLEFT)->GetAllButtonMappings()) {
            controller->GetButton(BTN_CUSTOM_OCARINA_NOTE_B4)->AddButtonMapping(mapping);
        }
        for (auto [id, mapping] : controller->GetButton(BTN_DUP)->GetAllButtonMappings()) {
            controller->GetButton(BTN_CUSTOM_OCARINA_NOTE_D5)->AddButtonMapping(mapping);
        }
    }
    UIWidgets::PopStyleHeader();
}

void Draw_Placements() {
    UIWidgets::PushStyleHeader(THEME_COLOR);
    ImGui::SeparatorText("General Margins Settings");
    UIWidgets::CVarSliderInt("Top: %dpx", CVAR_COSMETIC("HUD.Margin.T"),
                             UIWidgets::IntSliderOptions()
                                 .Min(static_cast<s16>(ImGui::GetWindowViewport()->Size.y / 2) * -1)
                                 .Max(25)
                                 .DefaultValue(0)
                                 .Size(ImVec2(300.0f, 0.0f))
                                 .Color(THEME_COLOR));
    UIWidgets::CVarSliderInt("Left: %dpx", CVAR_COSMETIC("HUD.Margin.L"),
                             UIWidgets::IntSliderOptions()
                                 .Min(-25)
                                 .Max(static_cast<s16>(ImGui::GetWindowViewport()->Size.x))
                                 .DefaultValue(0)
                                 .Size(ImVec2(300.0f, 0.0f))
                                 .Color(THEME_COLOR));
    UIWidgets::CVarSliderInt("Right: %dpx", CVAR_COSMETIC("HUD.Margin.R"),
                             UIWidgets::IntSliderOptions()
                                 .Min(static_cast<s16>(ImGui::GetWindowViewport()->Size.x) * -1)
                                 .Max(25)
                                 .DefaultValue(0)
                                 .Size(ImVec2(300.0f, 0.0f))
                                 .Color(THEME_COLOR));
    UIWidgets::CVarSliderInt("Bottom: %dpx", CVAR_COSMETIC("HUD.Margin.B"),
                             UIWidgets::IntSliderOptions()
                                 .Min(static_cast<s16>(ImGui::GetWindowViewport()->Size.y / 2) * -1)
                                 .Max(25)
                                 .DefaultValue(0)
                                 .Size(ImVec2(300.0f, 0.0f))
                                 .Color(THEME_COLOR));
    SetMarginAll("All margins on", true,
                 "Set most of the elements to use margins\nSome elements with default position will not be "
                 "affected\nElements without Anchor or Hidden will not be turned on");
    ImGui::SameLine();
    SetMarginAll("All margins off", false, "Set all of the elements to not use margins");
    ImGui::SameLine();
    ResetPositionAll();
    UIWidgets::Separator(true, true, 2.0f, 2.0f);
    if (ImGui::CollapsingHeader("Hearts count position")) {
        if (ImGui::BeginTable("tableHeartsCounts", 1, kTableFlags)) {
            ImGui::TableSetupColumn("Hearts counts settings", kCellFlags, kTableCellWidth);
            Table_InitHeader(false);
            DrawUseMarginsSlider("Hearts counts", CVAR_COSMETIC("HUD.Hearts"));
            DrawPositionsRadioBoxes(CVAR_COSMETIC("HUD.HeartsCount"));
            DrawPositionSlider(CVAR_COSMETIC("HUD.HeartsCount"), -22,
                               static_cast<s16>(ImGui::GetWindowViewport()->Size.y), -125,
                               static_cast<s16>(ImGui::GetWindowViewport()->Size.x));
            DrawScaleSlider(CVAR_COSMETIC("HUD.HeartsCount"), 0.7f);
            UIWidgets::CVarSliderInt(
                "Heart line length : %d", CVAR_COSMETIC("HUD.Hearts.LineLength"),
                UIWidgets::IntSliderOptions()
                    .Min(0)
                    .Max(20)
                    .DefaultValue(0)
                    .Size(ImVec2(300.0f, 0.0f))
                    .Color(THEME_COLOR)
                    .Tooltip("This will set the length of a row of hearts. Set to 0 for unlimited length."));
            ImGui::EndTable();
        }
    }
    if (ImGui::CollapsingHeader("Magic Meter position")) {
        if (ImGui::BeginTable("tablemmpos", 1, kTableFlags)) {
            ImGui::TableSetupColumn("Magic meter settings", kCellFlags, kTableCellWidth);
            Table_InitHeader(false);
            DrawUseMarginsSlider("Magic meter", CVAR_COSMETIC("HUD.MagicBar"));
            DrawPositionsRadioBoxes(CVAR_COSMETIC("HUD.MagicBar"));
            UIWidgets::CVarRadioButton(
                "Anchor to life bar", CVAR_COSMETIC("HUD.MagicBar.PosType"), 5,
                UIWidgets::RadioButtonsOptions()
                    .Color(THEME_COLOR)
                    .Tooltip("This will make your elements follow the bottom of the life meter"));
            DrawPositionSlider(CVAR_COSMETIC("HUD.MagicBar"), 0,
                               static_cast<s16>(ImGui::GetWindowViewport()->Size.y / 2), -5,
                               static_cast<s16>(ImGui::GetWindowViewport()->Size.x / 2));
            DrawScaleSlider(CVAR_COSMETIC("HUD.MagicBar"), 1.0f);
            ImGui::EndTable();
        }
    }
    if (CVarGetInteger(CVAR_ENHANCEMENT("VisualAgony"), 0) &&
        ImGui::CollapsingHeader("Visual stone of agony position")) {
        if (ImGui::BeginTable("tabledvisualstoneofagony", 1, kTableFlags)) {
            ImGui::TableSetupColumn("Visual stone of agony settings", kCellFlags, kTableCellWidth);
            Table_InitHeader(false);
            DrawUseMarginsSlider("Visual stone of agony", CVAR_COSMETIC("HUD.VisualSoA"));
            DrawPositionsRadioBoxes(CVAR_COSMETIC("HUD.VisualSoA"));
            s16 Min_X_VSOA = 0;
            s16 Max_X_VSOA = static_cast<s16>(ImGui::GetWindowViewport()->Size.x / 2);
            if (CVarGetInteger(CVAR_COSMETIC("HUD.VisualSoA.PosType"), 0) == ANCHOR_RIGHT) {
                Max_X_VSOA = 290;
            } else if (CVarGetInteger(CVAR_COSMETIC("HUD.VisualSoA.PosType"), 0) == HIDDEN) {
                Min_X_VSOA = static_cast<s16>(ImGui::GetWindowViewport()->Size.x / 2) * -1;
            }
            DrawPositionSlider(CVAR_COSMETIC("HUD.VisualSoA"), 0,
                               static_cast<s16>(ImGui::GetWindowViewport()->Size.y / 2), Min_X_VSOA, Max_X_VSOA);
            DrawScaleSlider(CVAR_COSMETIC("HUD.VisualSoA"), 1.0f);
            ImGui::EndTable();
        }
    }
    Draw_Table_Dropdown("B Button position", "tablebbtn", "B Button settings", "B Button", CVAR_COSMETIC("HUD.BButton"),
                        0, static_cast<int>(ImGui::GetWindowViewport()->Size.y / 4) + 50, -1,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.x) - 50, 0.95f);
    Draw_Table_Dropdown("A Button position", "tableabtn", "A Button settings", "A Button", CVAR_COSMETIC("HUD.AButton"),
                        -10, static_cast<int>(ImGui::GetWindowViewport()->Size.y / 4) + 50, -20,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.x) - 50, 0.95f);
    Draw_Table_Dropdown("Start Button position", "tablestartbtn", "Start Button settings", "Start Button",
                        CVAR_COSMETIC("HUD.StartButton"), 0, static_cast<int>(ImGui::GetWindowViewport()->Size.y / 2),
                        0, static_cast<int>(ImGui::GetWindowViewport()->Size.x / 2) + 70, 0.75f);
    C_Button_Dropdown("C Button Up position", "tablecubtn", "C Button Up settings", "C Button Up",
                      CVAR_COSMETIC("HUD.CUpButton"), CVAR_COSMETIC("HUD.CUpButton.PosType"), 0.5f);
    C_Button_Dropdown("C Button Down position", "tablecdbtn", "C Button Down settings", "C Button Down",
                      CVAR_COSMETIC("HUD.CDownButton"), CVAR_COSMETIC("HUD.CDownButton.PosType"), 0.87f);
    C_Button_Dropdown("C Button Left position", "tableclbtn", "C Button Left settings", "C Button Left",
                      CVAR_COSMETIC("HUD.CLeftButton"), CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0.87f);
    C_Button_Dropdown("C Button Right position", "tablecrbtn", "C Button Right settings", "C Button Right",
                      CVAR_COSMETIC("HUD.CRightButton"), CVAR_COSMETIC("HUD.CRightButton.PosType"), 0.87f);
    if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) && ImGui::CollapsingHeader("DPad items position")) {
        if (ImGui::BeginTable("tabledpaditems", 1, kTableFlags)) {
            ImGui::TableSetupColumn("DPad items settings", kCellFlags, kTableCellWidth);
            Table_InitHeader(false);
            DrawUseMarginsSlider("DPad items", CVAR_COSMETIC("HUD.Dpad"));
            DrawPositionsRadioBoxes(CVAR_COSMETIC("HUD.Dpad"));
            s16 Min_X_Dpad = 0;
            s16 Max_X_Dpad = static_cast<s16>(ImGui::GetWindowViewport()->Size.x / 2);
            if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == ANCHOR_RIGHT) {
                Max_X_Dpad = 290;
            } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == HIDDEN) {
                Min_X_Dpad = static_cast<s16>(ImGui::GetWindowViewport()->Size.x / 2) * -1;
            }
            DrawPositionSlider(CVAR_COSMETIC("HUD.Dpad"), 0, static_cast<s16>(ImGui::GetWindowViewport()->Size.y / 2),
                               Min_X_Dpad, Max_X_Dpad);
            DrawScaleSlider(CVAR_COSMETIC("HUD.Dpad"), 1.0f);
            ImGui::EndTable();
        }
    }
    Draw_Table_Dropdown("Minimaps position", "tableminimapspos", "minimaps settings", "Minimap",
                        CVAR_COSMETIC("HUD.Minimap"), static_cast<int>(ImGui::GetWindowViewport()->Size.y / 3) * -1,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.y / 3),
                        static_cast<int>(ImGui::GetWindowViewport()->Size.x) * -1,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.x / 2), 1.0f);
    Draw_Table_Dropdown("Small Keys counter position", "tablesmolekeys", "Small Keys counter settings",
                        "Small Keys counter", CVAR_COSMETIC("HUD.SmallKey"), 0,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.y / 3), -1,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.x / 2), 1.0f);
    Draw_Table_Dropdown("Rupee counter position", "tablerupeecount", "Rupee counter settings", "Rupee counter",
                        CVAR_COSMETIC("HUD.Rupees"), -2, static_cast<int>(ImGui::GetWindowViewport()->Size.y / 3), -3,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.x / 2), 1.0f);
    Draw_Table_Dropdown("Carrots position", "tableCarrots", "Carrots settings", "Carrots", CVAR_COSMETIC("HUD.Carrots"),
                        0, static_cast<int>(ImGui::GetWindowViewport()->Size.y / 2), -50,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.x / 2) + 25, 1.0f);
    Draw_Table_Dropdown("Timers position", "tabletimers", "Timers settings", "Timers", CVAR_COSMETIC("HUD.Timers"), 0,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.y / 2), -50,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.x / 2) - 50, 1.0f);
    Draw_Table_Dropdown("Archery Scores position", "tablearchery", "Archery Scores settings", "Archery scores",
                        CVAR_COSMETIC("HUD.ArcheryScore"), 0, static_cast<int>(ImGui::GetWindowViewport()->Size.y / 2),
                        -50, static_cast<int>(ImGui::GetWindowViewport()->Size.x / 2) - 50, 1.0f);
    Draw_Table_Dropdown("Title cards (Maps) position", "tabletcmaps", "Titlecard maps settings",
                        "Title cards (overworld)", CVAR_COSMETIC("HUD.TitleCard.Map"), 0,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.y / 2), -50,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.x / 2) + 10, 1.0f);
    Draw_Table_Dropdown("Title cards (Bosses) position", "tabletcbosses", "Title cards (Bosses) settings",
                        "Title cards (Bosses)", CVAR_COSMETIC("HUD.TitleCard.Boss"), 0,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.y / 2), -50,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.x / 2) + 10, 1.0f);
    Draw_Table_Dropdown("In-game Gameplay Timer position", "tablegameplaytimer", "In-game Gameplay Timer settings",
                        "In-game Gameplay Timer", CVAR_COSMETIC("HUD.IGT"), 0,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.y / 2), -50,
                        static_cast<int>(ImGui::GetWindowViewport()->Size.x / 2) + 10, 1.0f);
    if (ImGui::CollapsingHeader("Enemy Health Bar position")) {
        if (ImGui::BeginTable("enemyhealthbar", 1, kTableFlags)) {
            ImGui::TableSetupColumn("Enemy Health Bar settings", kCellFlags, kTableCellWidth);
            Table_InitHeader(false);
            std::string posTypeCVar = CVAR_COSMETIC("HUD.EnemyHealthBar.PosType");
            UIWidgets::CVarRadioButton(
                "Anchor to Enemy", CVAR_COSMETIC("HUD.EnemyHealthBar.PosType"), ENEMYHEALTH_ANCHOR_ACTOR,
                UIWidgets::RadioButtonsOptions().Color(THEME_COLOR).Tooltip("This will use enemy on screen position"));
            UIWidgets::CVarRadioButton(
                "Anchor to the top", CVAR_COSMETIC("HUD.EnemyHealthBar.PosType"), ENEMYHEALTH_ANCHOR_TOP,
                UIWidgets::RadioButtonsOptions()
                    .Color(THEME_COLOR)
                    .Tooltip("This will make your elements follow the top edge of your game window"));
            UIWidgets::CVarRadioButton(
                "Anchor to the bottom", CVAR_COSMETIC("HUD.EnemyHealthBar.PosType"), ENEMYHEALTH_ANCHOR_BOTTOM,
                UIWidgets::RadioButtonsOptions()
                    .Color(THEME_COLOR)
                    .Tooltip("This will make your elements follow the bottom edge of your game window"));
            DrawPositionSlider(CVAR_COSMETIC("HUD.EnemyHealthBar."), -SCREEN_HEIGHT, SCREEN_HEIGHT,
                               -static_cast<int>(ImGui::GetWindowViewport()->Size.x / 2),
                               static_cast<int>(ImGui::GetWindowViewport()->Size.x / 2));
            if (UIWidgets::CVarSliderInt("Health Bar Width: %d", CVAR_COSMETIC("HUD.EnemyHealthBar.Width.Value"),
                                         UIWidgets::IntSliderOptions()
                                             .Min(32)
                                             .Max(128)
                                             .DefaultValue(64)
                                             .Size(ImVec2(300.0f, 0.0f))
                                             .Color(THEME_COLOR)
                                             .Tooltip("This will change the width of the health bar"))) {
                CVarSetInteger(CVAR_COSMETIC("HUD.EnemyHealthBar.Width.Changed"), 1);
            }
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::CalcTextSize("g").y * 2));
            if (UIWidgets::Button("Reset##EnemyHealthBarWidth",
                                  UIWidgets::ButtonOptions().Size(ImVec2(80, 36)).Padding(ImVec2(5.0f, 0.0f)))) {
                CVarClear(CVAR_COSMETIC("HUD.EnemyHealthBar.Width.Value"));
                CVarClear(CVAR_COSMETIC("HUD.EnemyHealthBar.Width.Changed"));
            }
            ImGui::EndTable();
        }
    }
    UIWidgets::PopStyleHeader();
}
