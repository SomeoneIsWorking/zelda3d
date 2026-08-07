/**
 * ImGui primitives both games share -- separators, spacers, menu entries, text rendering.
 *
 * Second batch of the function-level extraction from UIWidgets.cpp (the first was ui_theming). The
 * file as a whole cannot be merged; these seven can, and the GATE each one had to pass is worth
 * stating, because identical bodies alone are NOT sufficient:
 *
 *   1. body byte-identical in both games;
 *   2. the header DECLARATIONS identical too, defaults and overload set included;
 *   3. no parameter type from the divergent part of UIWidgets.hpp.
 *
 * Rule 2 exists because of Tooltip: its `const char*` body is byte-identical in both games, but OoT
 * declares only a `std::string` overload and MM only a `const char*` one. Extracting on body
 * equality alone would have changed OoT's public API. Rule 3 exists because of RadioButton and
 * StateButton, which take RadioButtonsOptions/ButtonOptions -- structs that differ per game and live
 * in the very header that would have to include this one.
 *
 * Deliberately NOT here, with reasons, so nobody re-derives them:
 *   Tooltip        -- rule 2, overload sets differ (OoT has two, MM one)
 *   RadioButton    -- rule 3, takes RadioButtonsOptions
 *   StateButton    -- rule 3, takes ButtonOptions
 *   WrappedText    -- `currentLineLength` is `unsigned int` in OoT and `int` in MM. Almost certainly
 *                     harmless, but picking one is a deliberate change, not an extraction.
 *   ClampFloat     -- declared in neither header; a file-local helper in both.
 *   CVarCheckbox, CVarInputInt, CVarInputString, CVarSliderFloat, CVarSliderInt -- identical bodies,
 *                     but they call ShipInit::Init, which each game supplies from its own header
 *                     ("soh/ShipInit.hpp" vs "2s2h/ShipInit.hpp"). Shareable once that seam is
 *                     declared once, the way port/zelda3d_port_api.h did it.
 */

#ifndef ZELDA3D_SHARED_GUI_UI_PRIMITIVES_H
#define ZELDA3D_SHARED_GUI_UI_PRIMITIVES_H

#include <string>
#include <imgui.h>
#include "gui/ui_colors.h"

namespace UIWidgets {

void PaddedSeparator(bool padTop = true, bool padBottom = true, float extraVerticalTopPadding = 0.0f, float extraVerticalBottomPadding = 0.0f);
void Separator(bool padTop = true, bool padBottom = true, float extraVerticalTopPadding = 0.0f, float extraVerticalBottomPadding = 0.0f);
void Spacer(float height = 0.0f);
bool BeginMenu(const char* label, Colors color = Colors::LightBlue);
bool MenuItem(const char* label, const char* shortcut = NULL, Colors color = Colors::LightBlue);
void RenderText(ImVec2 pos, const char* text, const char* text_end, bool hide_text_after_hash);
float CalcComboWidth(const char* preview_value, ImGuiComboFlags flags);

} // namespace UIWidgets

#endif // ZELDA3D_SHARED_GUI_UI_PRIMITIVES_H
