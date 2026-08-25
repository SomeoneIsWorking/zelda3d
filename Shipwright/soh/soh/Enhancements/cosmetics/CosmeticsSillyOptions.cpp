#include "CosmeticsSillyOptions.h"

#include "CosmeticsEditorLayout.h"
#include "cosmeticsTypes.h"
#include "global.h"
#include "macros.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
#include "z64.h"
#include "z64save.h"
extern SaveContext gSaveContext;
extern PlayState* gPlayState;
}

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
}

namespace {
WidgetInfo goronNeck;
}

void Reset_Option_Single(const char* Button_Title, const char* name) {
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::CalcTextSize("g").y * 2));
    if (UIWidgets::Button(Button_Title, UIWidgets::ButtonOptions().Size(ImVec2(80, 36)).Padding(ImVec2(5.0f, 0.0f)))) {
        CVarClear(name);
    }
}

void Reset_Option_Double(const char* Button_Title, const char* name) {
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::CalcTextSize("g").y * 2));
    if (UIWidgets::Button(Button_Title, UIWidgets::ButtonOptions().Size(ImVec2(80, 36)).Padding(ImVec2(5.0f, 0.0f)))) {
        CVarClear((std::string(name) + ".Value").c_str());
        CVarClear((std::string(name) + ".Changed").c_str());
    }
}

void DrawSillyTab() {
    ImGui::BeginDisabled(CVarGetInteger(CVAR_SETTING("DisableChanges"), 0));

    UIWidgets::Separator(true, true, 2.0f, 2.0f);

    UIWidgets::CVarCheckbox(
        "Let It Snow", CVAR_GENERAL("LetItSnow"),
        UIWidgets::CheckboxOptions()
            .Color(THEME_COLOR)
            .Tooltip("Makes snow fall for December holidays.\nWill reset on restart outside of December 23-25."));

    UIWidgets::Separator(true, true, 2.0f, 2.0f);

    if (UIWidgets::CVarSliderFloat("Link Body Size", CVAR_COSMETIC("Link.BodySize.Value"),
                                   UIWidgets::FloatSliderOptions()
                                       .Format("%.3f")
                                       .Min(0.001f)
                                       .Max(0.05f)
                                       .DefaultValue(0.01f)
                                       .Step(0.001f)
                                       .Size(ImVec2(300.0f, 0.0f))
                                       .Color(THEME_COLOR))) {
        CVarSetInteger(CVAR_COSMETIC("Link.BodySize.Changed"), 1);
    }
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::CalcTextSize("g").y * 2));
    if (UIWidgets::Button("Reset##Link_BodySize",
                          UIWidgets::ButtonOptions().Size(ImVec2(80, 36)).Padding(ImVec2(5.0f, 0.0f)))) {
        CVarClear(CVAR_COSMETIC("Link.BodySize.Value"));
        CVarClear(CVAR_COSMETIC("Link.BodySize.Changed"));
        if (gPlayState != nullptr) {
            // Not static, for the reason given at the other site: once per process, written through
            // forever after.
            Player* player = GET_PLAYER(gPlayState);
            player->actor.scale.x = 0.01f;
            player->actor.scale.y = 0.01f;
            player->actor.scale.z = 0.01f;
        }
    }

    UIWidgets::Separator(true, true, 2.0f, 2.0f);
    if (UIWidgets::CVarSliderFloat("Link Head Scale", CVAR_COSMETIC("Link.HeadScale.Value"),
                                   UIWidgets::FloatSliderOptions()
                                       .Format("%.1fx")
                                       .Min(0.1f)
                                       .Max(5.0f)
                                       .DefaultValue(1.0f)
                                       .Step(0.1f)
                                       .Size(ImVec2(300.0f, 0.0f))
                                       .Color(THEME_COLOR))) {
        CVarSetInteger(CVAR_COSMETIC("Link.HeadScale.Changed"), 1);
    }
    Reset_Option_Double("Reset##Link_HeadScale", CVAR_COSMETIC("Link.HeadScale"));

    UIWidgets::Separator(true, true, 2.0f, 2.0f);

    if (UIWidgets::CVarSliderFloat("Link Sword Scale", CVAR_COSMETIC("Link.SwordScale.Value"),
                                   UIWidgets::FloatSliderOptions()
                                       .Format("%.1fx")
                                       .Min(0.1f)
                                       .Max(5.0f)
                                       .DefaultValue(1.0f)
                                       .Step(0.1f)
                                       .Size(ImVec2(300.0f, 0.0f))
                                       .Color(THEME_COLOR))) {
        CVarSetInteger(CVAR_COSMETIC("Link.SwordScale.Changed"), 1);
    }
    Reset_Option_Double("Reset##Link_SwordScale", CVAR_COSMETIC("Link.SwordScale"));

    UIWidgets::Separator(true, true, 2.0f, 2.0f);

    UIWidgets::CVarSliderFloat("Bunny Hood Length", CVAR_COSMETIC("BunnyHood.EarLength"),
                               UIWidgets::FloatSliderOptions()
                                   .Format("%.0f")
                                   .Min(-300.0f)
                                   .Max(1000.0f)
                                   .DefaultValue(0.0f)
                                   .Step(10.0f)
                                   .Size(ImVec2(300.0f, 0.0f))
                                   .Color(THEME_COLOR));
    Reset_Option_Single("Reset##BunnyHood_EarLength", CVAR_COSMETIC("BunnyHood.EarLength"));

    UIWidgets::Separator(true, true, 2.0f, 2.0f);

    UIWidgets::CVarSliderFloat("Bunny Hood Spread", CVAR_COSMETIC("BunnyHood.EarSpread"),
                               UIWidgets::FloatSliderOptions()
                                   .Format("%.0f")
                                   .Min(-300.0f)
                                   .Max(500.0f)
                                   .DefaultValue(0.0f)
                                   .Step(10.0f)
                                   .Size(ImVec2(300.0f, 0.0f))
                                   .Color(THEME_COLOR));
    Reset_Option_Single("Reset##BunnyHood_EarSpread", CVAR_COSMETIC("BunnyHood.EarSpread"));

    UIWidgets::Separator(true, true, 2.0f, 2.0f);

    SohGui::mSohMenu->MenuDrawItem(goronNeck, static_cast<uint32_t>(ImGui::GetContentRegionAvail().x), THEME_COLOR);
    Reset_Option_Single("Reset##Goron_NeckLength", CVAR_COSMETIC("Goron.NeckLength"));

    UIWidgets::Separator(true, true, 2.0f, 2.0f);

    UIWidgets::CVarCheckbox("Unfix Goron Spin", CVAR_COSMETIC("UnfixGoronSpin"),
                            UIWidgets::CheckboxOptions().Color(THEME_COLOR));

    UIWidgets::Separator(true, true, 2.0f, 2.0f);

    UIWidgets::CVarSliderFloat("Fairies Size", CVAR_COSMETIC("Fairies.Size"),
                               UIWidgets::FloatSliderOptions()
                                   .Format("%.1fx")
                                   .Min(0.1f)
                                   .Max(5.0f)
                                   .DefaultValue(1.0f)
                                   .Step(0.1f)
                                   .Size(ImVec2(300.0f, 0.0f))
                                   .Color(THEME_COLOR));
    Reset_Option_Single("Reset##Fairies_Size", CVAR_COSMETIC("Fairies.Size"));

    UIWidgets::Separator(true, true, 2.0f, 2.0f);

    UIWidgets::CVarSliderFloat("N64 Logo Spin Speed", CVAR_COSMETIC("N64Logo.SpinSpeed"),
                               UIWidgets::FloatSliderOptions()
                                   .Format("%.1fx")
                                   .Min(0.1f)
                                   .Max(5.0f)
                                   .DefaultValue(1.0f)
                                   .Step(0.1f)
                                   .Size(ImVec2(300.0f, 0.0f))
                                   .Color(THEME_COLOR));
    Reset_Option_Single("Reset##N64Logo_SpinSpeed", CVAR_COSMETIC("N64Logo.SpinSpeed"));

    UIWidgets::Separator(true, true, 2.0f, 2.0f);

    UIWidgets::CVarSliderFloat("Moon Size", CVAR_COSMETIC("Moon.Size"),
                               UIWidgets::FloatSliderOptions()
                                   .Format("%.1fx")
                                   .Min(0.1f)
                                   .Max(5.0f)
                                   .DefaultValue(1.0f)
                                   .Step(0.1f)
                                   .Size(ImVec2(300.0f, 0.0f))
                                   .Color(THEME_COLOR));
    Reset_Option_Single("Reset##Moon_Size", CVAR_COSMETIC("Moon.Size"));

    UIWidgets::Separator(true, true, 2.0f, 2.0f);

    if (UIWidgets::CVarSliderFloat("Kak Windmill Speed", CVAR_COSMETIC("Kak.Windmill_Speed.Value"),
                                   UIWidgets::FloatSliderOptions()
                                       .Format("%.0f")
                                       .Min(100.0f)
                                       .Max(6000.0f)
                                       .DefaultValue(100.0f)
                                       .Step(10.0f)
                                       .Size(ImVec2(300.0f, 0.0f))
                                       .Color(THEME_COLOR))) {
        CVarSetInteger(CVAR_COSMETIC("Kak.Windmill_Speed.Changed"), 1);
    }
    Reset_Option_Double("Reset##Kak_Windmill_Speed", CVAR_COSMETIC("Kak.Windmill_Speed"));

    UIWidgets::Separator(true, true, 2.0f, 2.0f);

    ImGui::EndDisabled();
}

void RegisterCosmeticWidgets() {
    goronNeck = { .name = "Goron Neck Length", .type = WidgetType::WIDGET_CVAR_SLIDER_FLOAT };
    goronNeck.CVar(CVAR_COSMETIC("Goron.NeckLength"))
        .Options(UIWidgets::FloatSliderOptions()
                     .Format("%.0f")
                     .Min(0.0f)
                     .Max(5000.0f)
                     .DefaultValue(0.0f)
                     .Step(10.0f)
                     .Size(ImVec2(300.0f, 0.0f))
                     .Color(THEME_COLOR));
    const auto& path = CosmeticsEditorLayout::GoronNeckSearchPath();
    SohGui::mSohMenu->AddSearchWidget({ goronNeck, path.category, path.window, path.tab });
}
