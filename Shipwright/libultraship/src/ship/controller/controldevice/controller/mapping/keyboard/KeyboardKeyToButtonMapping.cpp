#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardKeyToButtonMapping.h"
#include <spdlog/spdlog.h>
#include "ship/utils/StringHelper.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/Context.h"
#include <cstdio>
#include <unordered_map>

namespace Ship {
KeyboardKeyToButtonMapping::KeyboardKeyToButtonMapping(uint8_t portIndex, CONTROLLERBUTTONS_T bitmask,
                                                       KbScancode scancode)
    : ControllerInputMapping(PhysicalDeviceType::Keyboard), KeyboardKeyToAnyMapping(scancode),
      ControllerButtonMapping(PhysicalDeviceType::Keyboard, portIndex, bitmask) {
}

// Poll-time half of the ZELDA3D_DBG_INPUT diagnostic (debug_journal/2026-07-15-keyboard-headed-v2.md
// / 2026-07-15-phase1-input-consolidation.md), extended into this mapping so a poll where
// mKeyPressed is true can be told apart from one where the bit never reached the pad. Logs
// on-change only (per scancode): whether the key is latched pressed, whether this poll was
// blocked, and whether the bit was OR'd into padButtons. The event-time log (Ship::ControlDeck::
// ProcessKeyboardEvent) proves the SDL event reached ControlDeck at all; this proves the LATCHED
// press actually survived to the next WriteToOSContPad poll (ruling in/out a poll-time-only drop
// the event-time log can't see, e.g. a block flag that flips true between the keydown event and
// the next poll).
#include "ship/zelda3d_hostiface.h"
static void Zelda3dDbgInputLogPoll(KbScancode scancode, bool keyPressed, bool blocked, bool applied) {
    if (Zelda3D_HostDbgInputEnabled() == 0) {
        return;
    }
    struct State { bool keyPressed = false; bool blocked = false; bool applied = false; bool seen = false; };
    static std::unordered_map<int, State> sLast;
    State cur{ keyPressed, blocked, applied, true };
    State& last = sLast[static_cast<int>(scancode)];
    if (last.seen && last.keyPressed == cur.keyPressed && last.blocked == cur.blocked &&
        last.applied == cur.applied) {
        return;
    }
    last = cur;
    fprintf(stderr,
            "[zelda3d_dbg_input] poll scancode=%d keyPressed=%d KeyboardGameInputBlocked=%d "
            "appliedToPad=%d\n",
            static_cast<int>(scancode), keyPressed, blocked, applied);
}

void KeyboardKeyToButtonMapping::UpdatePad(CONTROLLERBUTTONS_T& padButtons) {
    bool blocked = Context::GetRawInstance()->GetControlDeck()->KeyboardGameInputBlocked();
    if (blocked) {
        Zelda3dDbgInputLogPoll(mKeyboardScancode, mKeyPressed, true, false);
        return;
    }

    if (!mKeyPressed) {
        Zelda3dDbgInputLogPoll(mKeyboardScancode, false, false, false);
        return;
    }

    padButtons |= mBitmask;
    Zelda3dDbgInputLogPoll(mKeyboardScancode, true, false, true);
}

int8_t KeyboardKeyToButtonMapping::GetMappingType() {
    return MAPPING_TYPE_KEYBOARD;
}

std::string KeyboardKeyToButtonMapping::GetButtonMappingId() {
    return StringHelper::Sprintf("P%d-B%d-KB%d", mPortIndex, mBitmask, mKeyboardScancode);
}

void KeyboardKeyToButtonMapping::SaveToConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".ButtonMappings." + GetButtonMappingId();
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetString(
        StringHelper::Sprintf("%s.ButtonMappingClass", mappingCvarKey.c_str()).c_str(), "KeyboardKeyToButtonMapping");
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.Bitmask", mappingCvarKey.c_str()).c_str(), mBitmask);
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.KeyboardScancode", mappingCvarKey.c_str()).c_str(), mKeyboardScancode);
    Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
}

void KeyboardKeyToButtonMapping::EraseFromConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".ButtonMappings." + GetButtonMappingId();

    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.ButtonMappingClass", mappingCvarKey.c_str()).c_str());
    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.Bitmask", mappingCvarKey.c_str()).c_str());
    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.KeyboardScancode", mappingCvarKey.c_str()).c_str());

    Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
}

std::string KeyboardKeyToButtonMapping::GetPhysicalDeviceName() {
    return KeyboardKeyToAnyMapping::GetPhysicalDeviceName();
}

std::string KeyboardKeyToButtonMapping::GetPhysicalInputName() {
    return KeyboardKeyToAnyMapping::GetPhysicalInputName();
}
} // namespace Ship
