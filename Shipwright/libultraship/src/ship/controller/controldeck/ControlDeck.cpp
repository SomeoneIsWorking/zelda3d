#include "ship/controller/controldeck/ControlDeck.h"

#include "ship/Context.h"
#include "ship/controller/controldevice/controller/Controller.h"
#include "ship/utils/StringHelper.h"
#include "ship/config/ConsoleVariable.h"
#include <imgui.h>
#include "ship/controller/controldevice/controller/mapping/mouse/WheelHandler.h"
#include "ship/window/Window.h"
#include "ship/window/gui/Gui.h"
#include <cstdlib>
#include <cstdio>

namespace Ship {

// Headed-keyboard-input diagnostic (debug_journal/2026-07-15-keyboard-headed-v2.md). Physical
// keyboard input reaching the game cannot be reproduced headless (the REPL injects pad state
// directly, bypassing the real SDL event -> ControlDeck path), so this is the tool a user runs
// in a real windowed session to pinpoint where a keypress is getting dropped: it logs the
// decisive blocking state at the moment of every real SDL key event. Gated behind
// ZELDA3D_DBG_INPUT=1 so it costs nothing by default; only fires on an actual key event (not a
// per-frame poll), so it is inherently "log on change" and cheap.
//
// The enabled-check itself is now a SINGLE source of truth in
// Shipwright/soh/src/zelda3d/input/zelda3d_input.cpp (Zelda3D_DbgInputEnabled) — this file used to
// keep its own private copy of the same env-check lambda, duplicated verbatim in the LUS:: half of
// this diagnostic (libultraship/controller/controldeck/ControlDeck.cpp). Forward-declared here
// rather than including a soh header: libultraship is a lower layer that soh links against, not
// the reverse (same one-directional pattern as Zelda3D_MeasureResult / Gui.cpp's native-HUD entry
// point).
#include "ship/zelda3d_hostiface.h"
static bool Zelda3dDbgInputEnabled() {
    return Zelda3D_HostDbgInputEnabled() != 0;
}

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
// v3: PC-native item bar (kanban #203) — the three C-button item slots moved off the arrow keys
//     onto 1/2/3, and C-Up (first-person look / Navi, not an item slot) onto C. The arrow keys are
//     now unbound, reserved for the camera in the mouse-look pass.
static constexpr int kZelda3dInputSchemeVersion = 3;
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

    if (Zelda3dDbgInputEnabled()) {
        // This line firing at all proves the raw SDL key event reached ControlDeck (rules out
        // "SDL isn't delivering keys"/Wayland focus as the drop point). If it never fires while
        // physically pressing keys in the headed build, the drop is upstream of here (SDL event
        // pump / window focus) and none of the flags below are reachable — go straight to
        // checking window focus / `SDL_GetKeyboardFocus()` on that build.
        bool menuOpen = false;
        if (auto ctx = Context::GetRawInstance(); ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
            menuOpen = ctx->GetWindow()->GetGui()->IsInteractiveMenuOpen();
        }
        fprintf(stderr,
                "[zelda3d_dbg_input] key event=%d scancode=%d consumed=%d | AllGameInputBlocked=%d "
                "KeyboardGameInputBlocked=%d RmlMenuOpen=%d\n",
                static_cast<int>(eventType), static_cast<int>(scancode), result, AllGameInputBlocked(),
                KeyboardGameInputBlocked(), menuOpen);
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
    // SoH3D: keyboard game input is blocked ONLY by the real interactive menu (RmlUi), never by
    // ImGui state. ImGui itself is stubbed out at runtime in this fork (see
    // imgui_shim/imgui_stub.cpp): ImGui::GetIO() returns a zero-initialized static ImGuiIO, and
    // ImGui::NewFrame() is never called anywhere (Gui::StartFrame()/ImGuiBackendNewFrame() are
    // no-ops), so `WantCaptureKeyboard` never gets recomputed and reading it is dead code by
    // construction — it happened to read false, but relying on a stub struct staying zeroed is
    // not a real invariant, and the prior `ActiveIdWindow->ID != main-game-window` version of
    // this check DID false-positive in a real windowed build (any lingering ImGui ActiveId, even
    // on a force-hidden window, blocked ALL game keyboard input — regression re-reported
    // 2026-07-15 even after the WantCaptureKeyboard swap, consistent with ImGui state never being
    // a reliable signal here at all).
    //
    // AllGameInputBlocked() is the correct and sufficient gate: it is driven by
    // SohRmlUi::SetVisible() registering/clearing ZELDA3D_RML_MENU_BLOCK_ID exactly while the
    // menu is open (ship/window/gui/rml/SohRmlUi.cpp). Reference (same bug class, known-good
    // fix): the sibling psxport gates its keyboard read purely on the overlay actually wanting
    // keyboard (runtime/recomp/pad_input.cpp — `rml_overlay.wantsKeyboard()`), never on ImGui
    // focus/capture state. User authorized removing ImGui from this path outright (2026-07-15).
    return AllGameInputBlocked();
}

bool ControlDeck::MouseGameInputBlocked() {
    // Same treatment the keyboard path above already got, and for the same reason -- plus this one
    // was an outright BUG. `ImGui::GetCurrentContext()` returns the shim's zeroed storage (never
    // null, by design), so HoveredWindow was always NULL and this returned true unconditionally:
    // mouse game input has been blocked in every frame since ImGui was removed.
    //
    // AllGameInputBlocked() is the correct and sufficient gate; SohRmlUi::SetVisible() registers and
    // clears ZELDA3D_RML_MENU_BLOCK_ID exactly while the menu is open.
    return AllGameInputBlocked();
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
