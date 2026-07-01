#include "libultraship/controller/controldevice/controller/Controller.h"
#include <memory>
#include <algorithm>
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#if __APPLE__
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#else
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#endif
#include <spdlog/spdlog.h>
#include <cstdio>
#include "ship/utils/StringHelper.h"

#define M_TAU 6.2831853071795864769252867665590057 // 2 * pi
#define MINIMUM_RADIUS_TO_MAP_NOTCH 0.9

// #32 hotswap — last-used input device signal, defined in zelda3d.c (C).
extern "C" {
    extern int gZelda3dInputDevice;
    // Gamepad hotbar: active slot selection + B-fire injection, defined in zelda3d.c.
    extern int gZelda3dHotbarActive;
    extern int gZelda3dHotbarFireB;
}

namespace LUS {

// ---- #32 button chords (modifier + face -> N64 item slots, no C-pad) ------------------------------
// SoH maps one physical input -> one N64 bit, with no native modifier/chord support. A chord (e.g.
// RB+A -> C-up) needs PHYSICAL input (X/Y have no N64 equivalent) AND suppression of the modifier's own
// action — and suppression cannot live in a single button mapping (it can only OR bits, and mapping
// order is unspecified). So chords are applied HERE, at the single pad-assembly chokepoint, after every
// button mapping has run. See docs/lus_input_architecture.md. The physical read is real SDL; a Zelda3D
// injection seam (Zelda3D_ChordPhysicalState) lets the pure logic be exercised headless (the physical SDL
// path itself needs an on-controller test — the headless harness only injects at the N64-pad level).

// Physical chord-input bits (device-agnostic; mapped from SDL below). Modifier + the four face buttons.
enum {
    CHORD_PHYS_MOD = 1 << 0, // RB / right shoulder (the chord modifier)
    CHORD_PHYS_A = 1 << 1,
    CHORD_PHYS_B = 1 << 2,
    CHORD_PHYS_X = 1 << 3,
    CHORD_PHYS_Y = 1 << 4,
};
struct ButtonChord {
    uint16_t physTrigger; // CHORD_PHYS_* face button that, with the modifier, fires this chord
    uint16_t n64Output;   // N64 bit produced (a C-button item slot)
    uint16_t n64Suppress; // N64 bit(s) to clear when it fires (the trigger's own action)
};
// Default Xbox scheme (no C-pad): RB + A/B/X/Y -> the four C-button item slots. A/B also normally fire
// N64 A/B, so suppress those when the chord fires; X/Y are unmapped on N64 so suppress nothing.
static const ButtonChord kDefaultChords[] = {
    { CHORD_PHYS_A, /*CRIGHT*/ 0x0001, /*A*/ 0x8000 },
    { CHORD_PHYS_B, /*CLEFT */ 0x0002, /*B*/ 0x4000 },
    { CHORD_PHYS_Y, /*CUP   */ 0x0008, 0x0000 },
    { CHORD_PHYS_X, /*CDOWN */ 0x0004, 0x0000 },
};

// Pure, testable: given the assembled N64 buttons + which physical chord-inputs are held, apply chords.
// When the modifier is held and a trigger fires, set the C-button, clear the trigger's own N64 bit, and
// clear the modifier's normal N64 R bit (RB = N64 R) so the modifier acts as a pure modifier.
CONTROLLERBUTTONS_T ApplyButtonChords(CONTROLLERBUTTONS_T buttons, uint16_t phys) {
    if (!(phys & CHORD_PHYS_MOD)) {
        return buttons;
    }
    bool anyFired = false;
    for (const auto& c : kDefaultChords) {
        if (phys & c.physTrigger) {
            buttons |= c.n64Output;
            buttons &= ~c.n64Suppress;
            anyFired = true;
        }
    }
    if (anyFired) {
        buttons &= ~0x0010; // clear N64 R (the RB modifier's normal action)
    }
    return buttons;
}

Controller::Controller(uint8_t portIndex, std::vector<CONTROLLERBUTTONS_T> bitmasks)
    : Ship::Controller(portIndex, bitmasks) {
}

void Controller::ReadToPad(void* pad) {
    ReadToOSContPad((OSContPad*)pad);
}

void Controller::ReadToOSContPad(OSContPad* pad) {
    OSContPad padToBuffer = { 0 };

    // Button Inputs
    for (auto [bitmask, button] : mButtons) {
        button->UpdatePad(padToBuffer.button);
    }

    // #32 button chords + hotbar gamepad routing:
    //
    // C-button chord (unchanged): RB + A/B/X/Y -> N64 C-Right/C-Left/C-Up/C-Down item slots.
    // This wires RB as the "second layer" modifier for the engine's C-button equip slots (DpadEquips
    // maps 4 more), giving full no-C-pad coverage. Gated by CVar gControllerChords (default on).
    //
    // Hotbar gamepad routing (new — #97):
    //   Without chord: B = hotbar slot 1 (slot 0), Y = hotbar slot 2 (slot 1).
    //     B is already N64 B, so the item fires via the existing SoH use-item path.
    //     Y has no default N64 mapping — we set the active slot AND inject gZelda3dHotbarFireB=1 so
    //     Zelda3D_HotbarSync injects a virtual B press this frame.
    //   With RB chord: A/B/X/Y -> hotbar slots 3/4/5/6 (select + fire B).
    //     The chord also fires C-button bits (from kDefaultChords above); those are now redundant
    //     for the hotbar path and harmless (engine will try to use the C-slot item, but
    //     gZelda3dHotbarFireB fires the hotbar item via B which takes priority in the item-use path).
    //
    // Physical state is real SDL for live play, OR the gChordPhysInject CVar for headless testing.
    auto cvars = Ship::Context::GetRawInstance()->GetConsoleVariables();
    if (mPortIndex == 0) {
        int32_t inject = cvars->GetInteger("gChordPhysInject", -1);
        uint16_t phys = 0;
        if (inject >= 0) {
            phys = (uint16_t)inject;
        } else {
            for (const auto& [instanceId, gamepad] :
                 Ship::Context::GetRawInstance()
                     ->GetControlDeck()
                     ->GetConnectedPhysicalDeviceManager()
                     ->GetConnectedSDLGamepadsForPort(mPortIndex)) {
                if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
                    phys |= CHORD_PHYS_MOD;
                if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) phys |= CHORD_PHYS_A;
                if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST)) phys |= CHORD_PHYS_B;
                if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST)) phys |= CHORD_PHYS_X;
                if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH)) phys |= CHORD_PHYS_Y;
            }
        }

        if (inject >= 0) {
            // Headless self-test: fold gChordTestButtons through the pure chord logic.
            CONTROLLERBUTTONS_T before = (CONTROLLERBUTTONS_T)cvars->GetInteger("gChordTestButtons", 0);
            CONTROLLERBUTTONS_T after = ApplyButtonChords(before, phys);
            printf("[CHORD] phys=0x%02x buttons 0x%04x -> 0x%04x\n", phys, before, after);
            fflush(stdout);
            padToBuffer.button |= after;
        } else if (cvars->GetInteger("gControllerChords", 1)) {
            // Live: C-button chord.
            if (phys != 0) {
                padToBuffer.button = ApplyButtonChords(padToBuffer.button, phys);
            }
        }

        // Hotbar gamepad routing (port 0 only).
        // Without chord modifier: B selects slot 0, Y selects slot 1.
        // With RB chord modifier: A->slot2, B->slot3, X->slot4, Y->slot5 (+ fire B).
        // Log whenever we change the active slot (once per press, visible in run.log).
        if (phys & CHORD_PHYS_MOD) {
            // Chord path: map A/B/X/Y -> hotbar slots 3-6.
            bool chordSlot = false;
            int newSlot = -1;
            if (phys & CHORD_PHYS_A) { newSlot = 2; chordSlot = true; }
            else if (phys & CHORD_PHYS_B) { newSlot = 3; chordSlot = true; }
            else if (phys & CHORD_PHYS_X) { newSlot = 4; chordSlot = true; }
            else if (phys & CHORD_PHYS_Y) { newSlot = 5; chordSlot = true; }
            if (chordSlot && newSlot != gZelda3dHotbarActive) {
                printf("[HOTBAR] chord RB+phys=0x%02x -> slot %d (fire B)\n", phys, newSlot);
                fflush(stdout);
                gZelda3dHotbarActive = newSlot;
                gZelda3dHotbarFireB = 1; // request B-fire from Zelda3D_HotbarSync this frame
                gZelda3dInputDevice = 0; // gamepad active
            } else if (chordSlot) {
                // Same slot already selected, still fire B.
                gZelda3dHotbarFireB = 1;
                gZelda3dInputDevice = 0;
            }
        } else {
            // No chord: B = slot 0 (B is already N64 B -> fires via existing path).
            // Y = slot 1 (inject B fire since Y has no default N64 mapping).
            if ((phys & CHORD_PHYS_B) && gZelda3dHotbarActive != 0) {
                printf("[HOTBAR] B -> slot 0\n"); fflush(stdout);
                gZelda3dHotbarActive = 0;
                gZelda3dInputDevice = 0;
                // N64 B is already set by the button mapping, no extra fire needed.
            } else if (phys & CHORD_PHYS_B) {
                gZelda3dInputDevice = 0; // B held, slot already 0
            } else if (phys & CHORD_PHYS_Y) {
                if (gZelda3dHotbarActive != 1) {
                    printf("[HOTBAR] Y -> slot 1 (fire B)\n"); fflush(stdout);
                    gZelda3dHotbarActive = 1;
                }
                gZelda3dHotbarFireB = 1; // Y has no default N64 mapping, inject B press
                gZelda3dInputDevice = 0;
            }
        }
    }

    // #32 hotswap: if any SDL gamepad has a button pressed on port 0 this frame, record it
    // as "gamepad last used" so the HUD swaps to Xbox glyphs. Only scan port 0 to avoid
    // spurious device flips from unmapped/background controllers.
    if (mPortIndex == 0) {
        bool anyGamepadBtn = false;
        for (const auto& [instanceId, gamepad] :
             Ship::Context::GetRawInstance()
                 ->GetControlDeck()
                 ->GetConnectedPhysicalDeviceManager()
                 ->GetConnectedSDLGamepadsForPort(mPortIndex)) {
            for (int btn = 0; btn < SDL_GAMEPAD_BUTTON_COUNT; btn++) {
                if (SDL_GetGamepadButton(gamepad, (SDL_GamepadButton)btn)) {
                    anyGamepadBtn = true;
                    break;
                }
            }
            if (!anyGamepadBtn) {
                // Also check for non-trivial axis deflection (>= 8192 = ~1/4 throw).
                for (int ax = 0; ax < SDL_GAMEPAD_AXIS_COUNT; ax++) {
                    Sint16 v = SDL_GetGamepadAxis(gamepad, (SDL_GamepadAxis)ax);
                    if (v > 8192 || v < -8192) { anyGamepadBtn = true; break; }
                }
            }
            if (anyGamepadBtn) break;
        }
        if (anyGamepadBtn) {
            gZelda3dInputDevice = 0; // gamepad
        }
    }

    // Stick Inputs
    GetLeftStick()->UpdatePad(padToBuffer.stick_x, padToBuffer.stick_y);
    GetRightStick()->UpdatePad(padToBuffer.right_stick_x, padToBuffer.right_stick_y);

    // Gyro
    GetGyro()->UpdatePad(padToBuffer.gyro_x, padToBuffer.gyro_y);

    mPadBuffer.push_front(padToBuffer);
    if (pad != nullptr) {
        auto& padFromBuffer = mPadBuffer[std::min(
            mPadBuffer.size() - 1,
            (size_t)Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_SIMULATED_INPUT_LAG, 0))];

        pad->button |= padFromBuffer.button;

        if (pad->stick_x == 0) {
            pad->stick_x = padFromBuffer.stick_x;
        }
        if (pad->stick_y == 0) {
            pad->stick_y = padFromBuffer.stick_y;
        }

        if (pad->right_stick_x == 0) {
            pad->right_stick_x = padFromBuffer.right_stick_x;
        }
        if (pad->right_stick_y == 0) {
            pad->right_stick_y = padFromBuffer.right_stick_y;
        }

        if (pad->gyro_x == 0) {
            pad->gyro_x = padFromBuffer.gyro_x;
        }
        if (pad->gyro_y == 0) {
            pad->gyro_y = padFromBuffer.gyro_y;
        }
    }

    while (mPadBuffer.size() > 6) {
        mPadBuffer.pop_back();
    }
}
} // namespace LUS
