#include "libultraship/controller/controldevice/controller/mapping/ControllerDefaultMappings.h"
#include "libultraship/libultra/controller.h"

namespace LUS {
ControllerDefaultMappings::ControllerDefaultMappings(
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<Ship::KbScancode>> defaultKeyboardKeyToButtonMappings,
    std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, Ship::KbScancode>>>
        defaultKeyboardKeyToAxisDirectionMappings,
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GamepadButton>>
        defaultSDLButtonToButtonMappings,
    std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, SDL_GamepadButton>>>
        defaultSDLButtonToAxisDirectionMappings,
    std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GamepadAxis, int32_t>>>
        defaultSDLAxisDirectionToButtonMappings,
    std::unordered_map<Ship::StickIndex,
                       std::vector<std::pair<Ship::Direction, std::pair<SDL_GamepadAxis, int32_t>>>>
        defaultSDLAxisDirectionToAxisDirectionMappings)
    : Ship::ControllerDefaultMappings(defaultKeyboardKeyToButtonMappings, defaultKeyboardKeyToAxisDirectionMappings,
                                      defaultSDLButtonToButtonMappings, defaultSDLButtonToAxisDirectionMappings,
                                      defaultSDLAxisDirectionToButtonMappings,
                                      defaultSDLAxisDirectionToAxisDirectionMappings) {
    SetDefaultKeyboardKeyToButtonMappings(defaultKeyboardKeyToButtonMappings);
    SetDefaultSDLButtonToButtonMappings(defaultSDLButtonToButtonMappings);
    SetDefaultSDLAxisDirectionToButtonMappings(defaultSDLAxisDirectionToButtonMappings);
}

ControllerDefaultMappings::ControllerDefaultMappings()
    : ControllerDefaultMappings(
          std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<Ship::KbScancode>>(),
          std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, Ship::KbScancode>>>(),
          std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GamepadButton>>(),
          std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, SDL_GamepadButton>>>(),
          std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GamepadAxis, int32_t>>>(),
          std::unordered_map<Ship::StickIndex,
                             std::vector<std::pair<Ship::Direction, std::pair<SDL_GamepadAxis, int32_t>>>>()) {
}

ControllerDefaultMappings::~ControllerDefaultMappings() {
}

void ControllerDefaultMappings::SetDefaultKeyboardKeyToButtonMappings(
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<Ship::KbScancode>> defaultKeyboardKeyToButtonMappings) {
    if (!defaultKeyboardKeyToButtonMappings.empty()) {
        Ship::ControllerDefaultMappings::SetDefaultKeyboardKeyToButtonMappings(defaultKeyboardKeyToButtonMappings);
        return;
    }

    // PC-native default keyboard layout (Zelda3D project vision: PC-native controls).
    // Move (W/A/S/D) is handled by the axis-direction defaults in Ship::ControllerDefaultMappings.
    //
    // Action          N64 btn    Key            Rationale
    // --------------- -------    ---            ---------
    // Interact/A      BTN_A      Space          Primary action — universal PC convention
    // Attack/B        BTN_B      F              Attack key left of WASD; LMB deferred (mouse-look pass)
    // Z-target        BTN_Z      Q              Lock-on left of WASD, reachable by pinky/ring finger
    // Shield/R        BTN_R      Left Ctrl      Modifier-style key for shield/roll
    // L-button        BTN_L      Left Shift     Walk-modifier / L in OoT
    // Pause/skip      BTN_START  Enter          Escape is reserved for the RmlUi menu (SohRmlUi
    //                                           toggles on ESC); Start on Enter keeps cutscene/dialog skip (#15)
    // Item slot 1     BTN_CLEFT  1              #203 — the C buttons ARE the item slots in OoT, so
    // Item slot 2     BTN_CDOWN  2              they get the PC item-bar keys. They were on the
    // Item slot 3     BTN_CRIGHT 3              arrow keys, which reads as "emulator with a keyboard"
    //                                           rather than a PC game and made the HUD badges show
    //                                           arrows on the item buttons.
    // Look/Navi       BTN_CUP    C              C-Up is the first-person look / Navi call, NOT an
    //                                           item slot, so it does not belong on the item bar.
    // D-pad up        BTN_DUP    I              Item slot up
    // D-pad down      BTN_DDOWN  K              Item slot down
    // D-pad left      BTN_DLEFT  J              Item slot left
    // D-pad right     BTN_DRIGHT L              Item slot right
    //
    // The arrow keys are deliberately left UNBOUND — they are the obvious home for the camera once
    // the mouse-look pass lands, and binding them to the item buttons in the meantime made the item
    // bar ambiguous (see the badge tie-break in zelda3d/input/zelda3d_keymap.cpp).
    // Mouse LMB→BTN_B and mouse-look→camera are deferred to the mouse-look pass.
    Ship::ControllerDefaultMappings::SetDefaultKeyboardKeyToButtonMappings({
        { BTN_A, { Ship::KbScancode::LUS_KB_SPACE } },
        { BTN_B, { Ship::KbScancode::LUS_KB_F } },
        { BTN_Z, { Ship::KbScancode::LUS_KB_Q } },
        { BTN_R, { Ship::KbScancode::LUS_KB_CONTROL } },
        { BTN_L, { Ship::KbScancode::LUS_KB_SHIFT } },
        { BTN_START, { Ship::KbScancode::LUS_KB_ENTER } },
        { BTN_CUP, { Ship::KbScancode::LUS_KB_C } },
        { BTN_CDOWN, { Ship::KbScancode::LUS_KB_2 } },
        { BTN_CLEFT, { Ship::KbScancode::LUS_KB_1 } },
        { BTN_CRIGHT, { Ship::KbScancode::LUS_KB_3 } },
        { BTN_DUP, { Ship::KbScancode::LUS_KB_I } },
        { BTN_DDOWN, { Ship::KbScancode::LUS_KB_K } },
        { BTN_DLEFT, { Ship::KbScancode::LUS_KB_J } },
        { BTN_DRIGHT, { Ship::KbScancode::LUS_KB_L } },
    });
}

void ControllerDefaultMappings::SetDefaultSDLButtonToButtonMappings(
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GamepadButton>>
        defaultSDLButtonToButtonMappings) {
    if (!defaultSDLButtonToButtonMappings.empty()) {
        Ship::ControllerDefaultMappings::SetDefaultSDLButtonToButtonMappings(defaultSDLButtonToButtonMappings);
        return;
    }

    // Modern dual-stick gamepad defaults (Xbox-style layout).
    // Right stick → C-buttons (camera) is handled via SDL axis defaults.
    // Left stick → move is handled via SDL axis defaults.
    //
    // N64 btn   Gamepad button         Rationale
    // -------   --------------         ---------
    // BTN_A     A (face south)         Confirm/interact — cross-platform standard
    // BTN_B     B (face east)          Back/attack
    // BTN_Z     Left trigger (axis)    Z-targeting (see axis→button defaults)
    // BTN_R     Right trigger (axis)   Shield/R (see axis→button defaults)
    // BTN_L     Left shoulder          L-button
    // BTN_START Start                  Pause/menu
    // D-pad     D-pad                  Item slots
    Ship::ControllerDefaultMappings::SetDefaultSDLButtonToButtonMappings({
        { BTN_A, { SDL_GAMEPAD_BUTTON_SOUTH } },
        { BTN_B, { SDL_GAMEPAD_BUTTON_EAST } },
        { BTN_L, { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER } },
        { BTN_START, { SDL_GAMEPAD_BUTTON_START } },
        { BTN_DUP, { SDL_GAMEPAD_BUTTON_DPAD_UP } },
        { BTN_DDOWN, { SDL_GAMEPAD_BUTTON_DPAD_DOWN } },
        { BTN_DLEFT, { SDL_GAMEPAD_BUTTON_DPAD_LEFT } },
        { BTN_DRIGHT, { SDL_GAMEPAD_BUTTON_DPAD_RIGHT } },
    });
}

void ControllerDefaultMappings::SetDefaultSDLAxisDirectionToButtonMappings(
    std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GamepadAxis, int32_t>>>
        defaultSDLAxisDirectionToButtonMappings) {
    if (!defaultSDLAxisDirectionToButtonMappings.empty()) {
        Ship::ControllerDefaultMappings::SetDefaultSDLAxisDirectionToButtonMappings(
            defaultSDLAxisDirectionToButtonMappings);
        return;
    }

    Ship::ControllerDefaultMappings::SetDefaultSDLAxisDirectionToButtonMappings({
        { BTN_R, { { SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1 } } },
        { BTN_Z, { { SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1 } } },
        { BTN_CUP, { { SDL_GAMEPAD_AXIS_RIGHTY, -1 } } },
        { BTN_CDOWN, { { SDL_GAMEPAD_AXIS_RIGHTY, 1 } } },
        { BTN_CLEFT, { { SDL_GAMEPAD_AXIS_RIGHTX, -1 } } },
        { BTN_CRIGHT, { { SDL_GAMEPAD_AXIS_RIGHTX, 1 } } },
    });
}
} // namespace LUS
