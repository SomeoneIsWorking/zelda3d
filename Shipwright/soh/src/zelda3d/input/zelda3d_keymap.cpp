// See zelda3d_keymap.h for why this module exists (kanban #203: the HUD key badges were baked
// artwork and had silently disagreed with the real bindings for months).
//
// Shape: ask the live ControlDeck which keyboard keys are bound to an N64 button, pick one
// deterministically, ask LUS for its human name (Window::GetKeyName -> SDL_GetScancodeName), and
// fold that name into something that fits on a keycap. Nothing here duplicates the binding state —
// it is a read-only view of it, which is the whole point.
#include "zelda3d_keymap.h"
#include "../hud/zelda3d_hud_assets.h"

#include "ship/Context.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/controldevice/controller/Controller.h"
#include "ship/controller/controldevice/controller/mapping/ControllerMapping.h"
#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardKeyToAnyMapping.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

namespace {

// SDL's scan-code names are written for a settings list ("Left Shift", "Page Down"), not for a
// 64-pixel keycap. Fold the ones that don't fit to a conventional keycap abbreviation. Everything
// SDL names in one character (letters, digits, punctuation) needs no entry and falls through.
// Matching is on the upper-cased, space-stripped name so "Left Ctrl"/"LeftCtrl" both hit.
const char* abbreviateKeyName(const std::string& squashed) {
    static const std::unordered_map<std::string, const char*> kAbbrev = {
        { "RETURN", "ENTER" },     { "KEYPADENTER", "KENT" },  { "ESCAPE", "ESC" },
        { "BACKSPACE", "BKSP" },   { "SPACE", "SPACE" },       { "CAPSLOCK", "CAPS" },
        { "PRINTSCREEN", "PRTSC" },{ "SCROLLLOCK", "SCRLK" },  { "PAUSE", "PAUSE" },
        { "INSERT", "INS" },       { "DELETE", "DEL" },        { "PAGEUP", "PGUP" },
        { "PAGEDOWN", "PGDN" },    { "NUMLOCK", "NUM" },       { "NUMLOCKCLEAR", "NUM" },
        { "LEFTCTRL", "LCTRL" },   { "RIGHTCTRL", "RCTRL" },   { "LEFTSHIFT", "LSHFT" },
        { "RIGHTSHIFT", "RSHFT" }, { "LEFTALT", "LALT" },      { "RIGHTALT", "RALT" },
        { "LEFTGUI", "LGUI" },     { "RIGHTGUI", "RGUI" },     { "APPLICATION", "MENU" },
    };
    auto it = kAbbrev.find(squashed);
    return it == kAbbrev.end() ? nullptr : it->second;
}

// Fold an SDL key name to the HUD glyph alphabet (assets/key_glyphs_png.h kKeyGlyphChars) and to a
// length a keycap can carry. Characters the alphabet lacks become '?' rather than vanishing, so a
// wrong-looking badge is visibly wrong instead of silently short.
std::string keycapLabel(const std::string& sdlName) {
    std::string squashed;
    squashed.reserve(sdlName.size());
    for (char c : sdlName) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            squashed.push_back((char)std::toupper(static_cast<unsigned char>(c)));
        }
    }
    if (squashed.empty()) {
        return "";
    }

    std::string label;
    if (const char* abbrev = abbreviateKeyName(squashed)) {
        label = abbrev;
    } else if (squashed.size() > 6 && squashed.compare(0, 6, "KEYPAD") == 0) {
        label = "K" + squashed.substr(6); // "Keypad 1" -> "K1", "Keypad /" -> "K/"
    } else {
        label = squashed;
    }

    // Anything still over five characters is a key name we have no abbreviation for; five is what a
    // 3x-wide cap holds at legible size (Zelda3D_KeyCapTex).
    if (label.size() > 5) {
        label.resize(5);
    }
    for (char& c : label) {
        if (std::strchr(Zelda3D_KeyCapAlphabet(), c) == nullptr) {
            c = '?';
        }
    }
    return label;
}

} // namespace

extern "C" const char* Zelda3D_KeyLabelForButton(unsigned short bitmask) {
    // One cached string per button: the HUD asks every frame, but the answer only changes on a
    // rebind. Also gives the caller a pointer that outlives the call.
    static std::unordered_map<unsigned short, std::string> sLabels;
    std::string& out = sLabels[bitmask];
    out.clear();

    auto* ctx = Ship::Context::GetRawInstance();
    if (ctx == nullptr) {
        return out.c_str();
    }
    auto controlDeck = ctx->GetControlDeck();
    if (controlDeck == nullptr) {
        return out.c_str();
    }
    auto controller = controlDeck->GetControllerByPort(0);
    if (controller == nullptr) {
        return out.c_str();
    }
    auto button = controller->GetButton(bitmask);
    if (button == nullptr) {
        return out.c_str();
    }

    // GetAllButtonMappings() is an unordered_map, so "the first keyboard mapping" is not a stable
    // notion — pick the lowest scancode so the badge can't flicker between two bound keys.
    std::shared_ptr<Ship::KeyboardKeyToAnyMapping> chosen;
    for (auto& [id, mapping] : button->GetAllButtonMappings()) {
        if (mapping == nullptr || mapping->GetMappingType() != MAPPING_TYPE_KEYBOARD) {
            continue;
        }
        auto kb = std::dynamic_pointer_cast<Ship::KeyboardKeyToAnyMapping>(mapping);
        if (kb == nullptr) {
            continue;
        }
        if (chosen == nullptr || kb->GetKeyboardScancode() < chosen->GetKeyboardScancode()) {
            chosen = kb;
        }
    }
    if (chosen != nullptr) {
        out = keycapLabel(chosen->GetPhysicalInputName());
    }
    return out.c_str();
}
