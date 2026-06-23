#include "libultraship/controller/controldevice/controller/Controller.h"
#include <memory>
#include <algorithm>
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#if __APPLE__
#include <SDL_events.h>
#include <SDL_gamecontroller.h>
#else
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_gamecontroller.h>
#endif
#include <spdlog/spdlog.h>
#include <cstdio>
#include "ship/utils/StringHelper.h"

#define M_TAU 6.2831853071795864769252867665590057 // 2 * pi
#define MINIMUM_RADIUS_TO_MAP_NOTCH 0.9

// #32 hotswap — last-used input device signal, defined in soh3d.c (C).
extern "C" { extern int gSoH3dInputDevice; }

namespace LUS {

// ---- #32 button chords (modifier + face -> N64 item slots, no C-pad) ------------------------------
// SoH maps one physical input -> one N64 bit, with no native modifier/chord support. A chord (e.g.
// RB+A -> C-up) needs PHYSICAL input (X/Y have no N64 equivalent) AND suppression of the modifier's own
// action — and suppression cannot live in a single button mapping (it can only OR bits, and mapping
// order is unspecified). So chords are applied HERE, at the single pad-assembly chokepoint, after every
// button mapping has run. See docs/lus_input_architecture.md. The physical read is real SDL; a SoH3D
// injection seam (SoH3D_ChordPhysicalState) lets the pure logic be exercised headless (the physical SDL
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

    // #32 button chords: after the pad is assembled, fold in modifier+face -> C-button item slots.
    // Gated by CVar gControllerChords (default on). Physical state is real SDL, OR a headless injection
    // via gChordPhysInject (>=0 -> use that bitfield directly; -1 -> read SDL). See ApplyButtonChords.
    auto cvars = Ship::Context::GetRawInstance()->GetConsoleVariables();
    if (cvars->GetInteger("gControllerChords", 1)) {
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
                if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
                    phys |= CHORD_PHYS_MOD;
                if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_A)) phys |= CHORD_PHYS_A;
                if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_B)) phys |= CHORD_PHYS_B;
                if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_X)) phys |= CHORD_PHYS_X;
                if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_Y)) phys |= CHORD_PHYS_Y;
            }
        }
        if (inject >= 0) {
            // Headless self-test: fold gChordTestButtons (pre-existing N64 bits) through the pure chord
            // logic so OUTPUT and SUPPRESSION are both observable without a controller, then OR into pad.
            CONTROLLERBUTTONS_T before = (CONTROLLERBUTTONS_T)cvars->GetInteger("gChordTestButtons", 0);
            CONTROLLERBUTTONS_T after = ApplyButtonChords(before, phys);
            printf("[CHORD] phys=0x%02x buttons 0x%04x -> 0x%04x\n", phys, before, after);
            fflush(stdout);
            padToBuffer.button |= after;
        } else if (phys != 0) {
            padToBuffer.button = ApplyButtonChords(padToBuffer.button, phys);
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
            for (int btn = 0; btn < SDL_CONTROLLER_BUTTON_MAX; btn++) {
                if (SDL_GameControllerGetButton(gamepad, (SDL_GameControllerButton)btn)) {
                    anyGamepadBtn = true;
                    break;
                }
            }
            if (!anyGamepadBtn) {
                // Also check for non-trivial axis deflection (>= 8192 = ~1/4 throw).
                for (int ax = 0; ax < SDL_CONTROLLER_AXIS_MAX; ax++) {
                    Sint16 v = SDL_GameControllerGetAxis(gamepad, (SDL_GameControllerAxis)ax);
                    if (v > 8192 || v < -8192) { anyGamepadBtn = true; break; }
                }
            }
            if (anyGamepadBtn) break;
        }
        if (anyGamepadBtn) {
            gSoH3dInputDevice = 0; // gamepad
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
