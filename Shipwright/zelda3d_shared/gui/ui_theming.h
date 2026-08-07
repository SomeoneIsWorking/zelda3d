/**
 * The ImGui style push/pop family, shared by both games.
 *
 * 26 functions that were duplicated in soh/SohGui/UIWidgets.cpp and 2s2h/BenGui/UIWidgets.cpp. 25 of
 * the 26 had BYTE-IDENTICAL bodies; the 26th differed by one number (see ui_theming.cpp). They touch
 * nothing but ImGui and the shared colour palette -- no CVars, no game types -- which is what makes
 * them extractable while the rest of UIWidgets is not. The DECLARATIONS below, default
 * arguments included, were byte-identical in the two games' headers.
 *
 * This is a FUNCTION-level extraction, deliberately. UIWidgets.cpp as a whole differs in 471 lines
 * and cannot be merged: among the differing functions is GetRandomValue, whose signature differs so
 * the two games have different determinism contracts. Sharing the file would have been wrong;
 * sharing this family is not.
 */

#ifndef ZELDA3D_SHARED_GUI_UI_THEMING_H
#define ZELDA3D_SHARED_GUI_UI_THEMING_H

#include <imgui.h>
#include "gui/ui_colors.h"

namespace UIWidgets {

void PushStyleMenu(const ImVec4& color);
void PushStyleMenu(Colors color = Colors::LightBlue);
void PopStyleMenu();
void PushStyleMenuItem(const ImVec4& color);
void PushStyleMenuItem(Colors color = Colors::LightBlue);
void PopStyleMenuItem();
void PushStyleButton(const ImVec4& color, ImVec2 padding = ImVec2(10.0f, 8.0f));
void PushStyleButton(Colors color = Colors::Gray, ImVec2 padding = ImVec2(10.0f, 8.0f));
void PopStyleButton();
void PushStyleCheckbox(const ImVec4& color, ImVec2 padding = ImVec2(10.0f, 6.0f));
void PushStyleCheckbox(Colors color = Colors::LightBlue, ImVec2 padding = ImVec2(10.0f, 6.0f));
void PopStyleCheckbox();
void PushStyleCombobox(const ImVec4& color);
void PushStyleCombobox(Colors color = Colors::LightBlue);
void PopStyleCombobox();
void PushStyleTabs(const ImVec4& color);
void PushStyleTabs(Colors color = Colors::LightBlue);
void PopStyleTabs();
void PushStyleInput(const ImVec4& color);
void PushStyleInput(Colors color = Colors::LightBlue);
void PopStyleInput();
void PushStyleHeader(const ImVec4& color);
void PushStyleHeader(Colors color = Colors::LightBlue);
void PopStyleHeader();
void PushStyleSlider(Colors color = Colors::LightBlue);
void PopStyleSlider();

} // namespace UIWidgets

#endif // ZELDA3D_SHARED_GUI_UI_THEMING_H
