/**
 * The menu colour palette, shared by both games.
 *
 * These lines were BYTE-IDENTICAL in soh/SohGui/UIWidgets.hpp and 2s2h/BenGui/UIWidgets.hpp. The
 * rest of those two files is not shareable and is not close to it -- ~52% of UIWidgets.hpp differs,
 * with genuinely divergent widget APIs on each side -- but this block is self-contained: an enum and
 * a lookup table, no CVars, no game types, ImGui only.
 *
 * It declares `namespace UIWidgets` itself, and each game includes it above its own reopening of
 * that namespace. So `UIWidgets::Colors` resolves exactly as before on both sides and no call site
 * changes.
 *
 * `ColorValues` is a namespace-scope `const`, which has internal linkage -- one copy per translation
 * unit. That was already true when it lived in each game's header; extracting it preserves that
 * rather than introducing it. Do not "fix" it to `inline` without checking both games: changing
 * linkage is a different thing from changing where the code lives.
 */

#ifndef ZELDA3D_SHARED_GUI_UI_COLORS_H
#define ZELDA3D_SHARED_GUI_UI_COLORS_H

#include <unordered_map>
#include <imgui.h>

namespace UIWidgets {

// clang-format off
enum Colors {
    Red,
    DarkRed,
    Orange,
    Green,
    DarkGreen,
    LightBlue,
    Blue,
    DarkBlue,
    Indigo,
    Violet,
    Purple,
    Brown,
    Gray,
    DarkGray,
    // not suitable for menu theme use
    Pink,
    Yellow,
    Cyan,
    Black,
    LightGray,
    White,
    NoColor
};

enum InputTypes { String, Scalar };

const std::unordered_map<Colors, ImVec4> ColorValues = {
    { Colors::Pink, ImVec4(0.87f, 0.3f, 0.87f, 1.0f) },     { Colors::Red, ImVec4(0.55f, 0.0f, 0.0f, 1.0f) },
    { Colors::DarkRed, ImVec4(0.3f, 0.0f, 0.0f, 1.0f) },    { Colors::Orange, ImVec4(0.85f, 0.55f, 0.0f, 1.0f) },
    { Colors::Yellow, ImVec4(0.95f, 0.95f, 0.0f, 1.0f) },   { Colors::Green, ImVec4(0.0f, 0.55f, 0.0f, 1.0f) },
    { Colors::DarkGreen, ImVec4(0.0f, 0.3f, 0.0f, 1.0f) },  { Colors::Cyan, ImVec4(0.0f, 0.9f, 0.9f, 1.0f) },
    { Colors::LightBlue, ImVec4(0.0f, 0.24f, 0.8f, 1.0f) }, { Colors::Blue, ImVec4(0.08f, 0.03f, 0.65f, 1.0f) },
    { Colors::DarkBlue, ImVec4(0.03f, 0.0f, 0.5f, 1.0f) },  { Colors::Indigo, ImVec4(0.35f, 0.0f, 0.87f, 1.0f) },
    { Colors::Violet, ImVec4(0.5f, 0.0f, 0.9f, 1.0f) },     { Colors::Purple, ImVec4(0.31f, 0.0f, 0.67f, 1.0f) },
    { Colors::Brown, ImVec4(0.37f, 0.18f, 0.0f, 1.0f) },    { Colors::LightGray, ImVec4(0.75f, 0.75f, 0.75f, 1.0f) },
    { Colors::Gray, ImVec4(0.45f, 0.45f, 0.45f, 1.0f) },    { Colors::DarkGray, ImVec4(0.15f, 0.15f, 0.15f, 1.0f) },
    { Colors::Black, ImVec4(0.0f, 0.0f, 0.0f, 1.0f) },      { Colors::White, ImVec4(1.0f, 1.0f, 1.0f, 1.0f) },
    { Colors::NoColor, ImVec4(0.0f, 0.0f, 0.0f, 0.0f) },
};

// clang-format on

} // namespace UIWidgets

#endif // ZELDA3D_SHARED_GUI_UI_COLORS_H
