#include "ship/controller/controldeck/ControlDeck.h"

#include "ship/Context.h"
#include "ship/controller/controldevice/controller/Controller.h"
#include "ship/utils/StringHelper.h"
#include "ship/config/ConsoleVariable.h"
#include <imgui.h>
#include "ship/controller/controldevice/controller/mapping/mouse/WheelHandler.h"

namespace Ship {

ControlDeck::ControlDeck(std::vector<CONTROLLERBUTTONS_T> additionalBitmasks,
                         std::shared_ptr<ControllerDefaultMappings> controllerDefaultMappings,
                         std::unordered_map<CONTROLLERBUTTONS_T, std::string> buttonNames) {
    mConnectedPhysicalDeviceManager = std::make_shared<ConnectedPhysicalDeviceManager>();
    mGlobalSDLDeviceSettings = std::make_shared<GlobalSDLDeviceSettings>();
    mControllerDefaultMappings = controllerDefaultMappings == nullptr ? std::make_shared<ControllerDefaultMappings>()
                                                                      : controllerDefaultMappings;
}

ControlDeck::~ControlDeck() {
    SPDLOG_TRACE("destruct control deck");
}

// Zelda3D PC-native input scheme version. Bump this integer whenever the keyboard default
// mapping table changes (LUS::ControllerDefaultMappings::SetDefaultKeyboardKeyToButtonMappings).
// On startup, if the stored scheme version in the config doesn't match, existing keyboard
// bindings are wiped and the new defaults are written — a one-time migration per user.
// v2: BTN_START moved off Escape onto Enter, so Escape only opens the RmlUi menu (was opening the
//     N64 pause/inventory AND the menu). Bumping re-migrates existing configs to the new defaults.
static constexpr int kZelda3dInputSchemeVersion = 2;
static constexpr const char* kZelda3dInputSchemeVersionCvar = "gZelda3dInputSchemeVersion";

void ControlDeck::Init(uint8_t* controllerBits) {
    mControllerBits = controllerBits;
    *mControllerBits |= 1 << 0;

    for (auto port : mPorts) {
        if (port->GetConnectedController()->HasConfig()) {
            port->GetConnectedController()->ReloadAllMappingsFromConfig();
        }
    }

    // if we don't have a config for controller 1, set default bindings
    if (!mPorts[0]->GetConnectedController()->HasConfig()) {
        mPorts[0]->GetConnectedController()->AddDefaultMappings(PhysicalDeviceType::Keyboard);
        mPorts[0]->GetConnectedController()->AddDefaultMappings(PhysicalDeviceType::Mouse);
        mPorts[0]->GetConnectedController()->AddDefaultMappings(PhysicalDeviceType::SDLGamepad);
    } else {
        // Migration: if the stored scheme version is stale, replace keyboard bindings with
        // the current PC-native defaults. Gamepad and mouse bindings are left untouched.
        int storedVersion = Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            kZelda3dInputSchemeVersionCvar, 0);
        if (storedVersion < kZelda3dInputSchemeVersion) {
            SPDLOG_INFO("[Zelda3D] Input scheme migrated: v{} -> v{} (resetting keyboard defaults)",
                        storedVersion, kZelda3dInputSchemeVersion);
            mPorts[0]->GetConnectedController()->ClearAllMappingsForDeviceType(PhysicalDeviceType::Keyboard);
            mPorts[0]->GetConnectedController()->AddDefaultMappings(PhysicalDeviceType::Keyboard);
            Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
                kZelda3dInputSchemeVersionCvar, kZelda3dInputSchemeVersion);
            Context::GetRawInstance()->GetConsoleVariables()->Save();
        }
    }
}

bool ControlDeck::ProcessKeyboardEvent(KbEventType eventType, KbScancode scancode) {
    bool result = false;
    for (auto port : mPorts) {
        auto controller = port->GetConnectedController();

        if (controller != nullptr) {
            result = controller->ProcessKeyboardEvent(eventType, scancode) || result;
        }
    }

    return result;
}

bool ControlDeck::ProcessMouseButtonEvent(bool isPressed, MouseBtn button) {
    bool result = false;
    for (auto port : mPorts) {
        auto controller = port->GetConnectedController();

        if (controller != nullptr) {
            result = controller->ProcessMouseButtonEvent(isPressed, button) || result;
        }
    }

    return result;
}

bool ControlDeck::AllGameInputBlocked() {
    return !mGameInputBlockers.empty();
}

bool ControlDeck::GamepadGameInputBlocked() {
    // block controller input when using the controller to navigate imgui menus
    return AllGameInputBlocked() ||
           Context::GetRawInstance()->GetWindow()->GetGui()->GetMenuOrMenubarVisible() &&
               Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_IMGUI_CONTROLLER_NAV, 0);
}

bool ControlDeck::KeyboardGameInputBlocked() {
    // SoH3D: block keyboard game input only when the UI genuinely wants the keyboard.
    //   - AllGameInputBlocked() covers the RmlUi menu — the interactive menu in this fork, which
    //     registers a game-input blocker via BlockGameInput() while open (SohRmlUi::SetVisible).
    //   - ImGui::GetIO().WantCaptureKeyboard covers a VISIBLE ImGui dev window being typed into;
    //     it is false in normal play (the legacy ImGui menu is force-hidden — SohGui::SetupMenu).
    // The previous `ActiveIdWindow->ID != main-game-window` heuristic false-positived in the real
    // windowed build: any lingering ImGui ActiveId (even on a hidden window) blocked ALL game
    // keyboard input, killing input at the title and in-game (regression re-reported 2026-07-15).
    // Reference (same bug class, known-good fix): the sibling psxport gates its keyboard read
    // purely on the overlay actually wanting keyboard (runtime/recomp/pad_input.cpp —
    // `rml_overlay.wantsKeyboard()`), not on stale ImGui focus state.
    return AllGameInputBlocked() || ImGui::GetIO().WantCaptureKeyboard;
}

bool ControlDeck::MouseGameInputBlocked() {
    // block mouse input when user interacting with gui
    ImGuiWindow* window = ImGui::GetCurrentContext()->HoveredWindow;
    if (window == NULL) {
        return true;
    }
    return AllGameInputBlocked() ||
           (window->ID != Context::GetRawInstance()->GetWindow()->GetGui()->GetMainGameWindowID());
}

std::shared_ptr<Controller> ControlDeck::GetControllerByPort(uint8_t port) {
    return mPorts[port]->GetConnectedController();
}

void ControlDeck::BlockGameInput(int32_t blockId) {
    mGameInputBlockers[blockId] = true;
}

void ControlDeck::UnblockGameInput(int32_t blockId) {
    mGameInputBlockers.erase(blockId);
}

std::shared_ptr<ConnectedPhysicalDeviceManager> ControlDeck::GetConnectedPhysicalDeviceManager() {
    return mConnectedPhysicalDeviceManager;
}

std::shared_ptr<GlobalSDLDeviceSettings> ControlDeck::GetGlobalSDLDeviceSettings() {
    return mGlobalSDLDeviceSettings;
}

std::shared_ptr<ControllerDefaultMappings> ControlDeck::GetControllerDefaultMappings() {
    return mControllerDefaultMappings;
}

const std::unordered_map<CONTROLLERBUTTONS_T, std::string>& ControlDeck::GetAllButtonNames() const {
    return mButtonNames;
}

std::string ControlDeck::GetButtonNameForBitmask(CONTROLLERBUTTONS_T bitmask) {
    // if we don't have a name for this bitmask,
    // return the stringified bitmask
    if (!mButtonNames.contains(bitmask)) {
        return std::to_string(bitmask);
    }

    return mButtonNames[bitmask];
}
} // namespace Ship
