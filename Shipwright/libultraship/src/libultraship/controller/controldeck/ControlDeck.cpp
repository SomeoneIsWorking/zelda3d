#include "libultraship/controller/controldeck/ControlDeck.h"

#include "ship/Context.h"
#include "libultraship/controller/controldevice/controller/Controller.h"
#include "libultraship/controller/controldevice/controller/mapping/ControllerDefaultMappings.h"
#include "ship/utils/StringHelper.h"
#include <imgui.h>
#include "ship/controller/controldevice/controller/mapping/mouse/WheelHandler.h"
#include "ship/controller/scripted/ScriptedInput.h"
#include <cstdlib>
#include <cstdio>

namespace LUS {

// See ship/controller/controldeck/ControlDeck.cpp for the per-key-event half of this diagnostic
// (ZELDA3D_DBG_INPUT=1). This half runs every frame regardless of whether any key event fired, so
// it catches the case where SDL delivers NO key events at all (e.g. a Wayland focus problem): if
// the user mashes a key and neither this nor the per-event log ever prints a changed/blocked
// line, the drop is upstream of ControlDeck entirely.
//
// The enabled-check is a SINGLE source of truth in
// Shipwright/soh/src/zelda3d/input/zelda3d_input.cpp (Zelda3D_DbgInputEnabled) — see that file's
// comment and the matching forward-decl in ship/controller/controldeck/ControlDeck.cpp for why
// this is a local extern "C" decl rather than a soh header include.
#include "ship/zelda3d_hostiface.h"
static bool Zelda3dDbgInputEnabled() {
    return Zelda3D_HostDbgInputEnabled() != 0;
}
ControlDeck::ControlDeck(std::vector<CONTROLLERBUTTONS_T> additionalBitmasks,
                         std::shared_ptr<Ship::ControllerDefaultMappings> controllerDefaultMappings,
                         std::unordered_map<CONTROLLERBUTTONS_T, std::string> buttonNames)
    : Ship::ControlDeck(additionalBitmasks, controllerDefaultMappings, buttonNames), mPads(nullptr) {
    std::vector<CONTROLLERBUTTONS_T> bitmasks;
    for (auto [bitmask, name] : buttonNames) {
        bitmasks.push_back(bitmask);
    }
    bitmasks.insert(bitmasks.end(), additionalBitmasks.begin(), additionalBitmasks.end());
    for (int32_t i = 0; i < MAXCONTROLLERS; i++) {
        mPorts.push_back(std::make_shared<Ship::ControlPort>(i, std::make_shared<Controller>(i, bitmasks)));
    }
}

ControlDeck::ControlDeck(std::vector<CONTROLLERBUTTONS_T> additionalBitmasks)
    : ControlDeck(additionalBitmasks, std::make_shared<LUS::ControllerDefaultMappings>(),
                  std::unordered_map<CONTROLLERBUTTONS_T, std::string>({
                      { BTN_A, "A" },
                      { BTN_B, "B" },
                      { BTN_L, "L" },
                      { BTN_R, "R" },
                      { BTN_Z, "Z" },
                      { BTN_START, "Start" },
                      { BTN_CLEFT, "CLeft" },
                      { BTN_CRIGHT, "CRight" },
                      { BTN_CUP, "CUp" },
                      { BTN_CDOWN, "CDown" },
                      { BTN_DLEFT, "DLeft" },
                      { BTN_DRIGHT, "DRight" },
                      { BTN_DUP, "DUp" },
                      { BTN_DDOWN, "DDown" },
                  })) {
}

ControlDeck::ControlDeck() : ControlDeck(std::vector<CONTROLLERBUTTONS_T>()) {
}

OSContPad* ControlDeck::GetPads() {
    return mPads;
}

void ControlDeck::WriteToPad(void* pad) {
    WriteToOSContPad((OSContPad*)pad);
}

void ControlDeck::WriteToOSContPad(OSContPad* pad) {
    SDL_PumpEvents();
    Ship::WheelHandler::GetInstance()->Update();

    if (Zelda3dDbgInputEnabled()) {
        static bool sLastBlocked = false;
        bool blocked = AllGameInputBlocked();
        if (blocked != sLastBlocked) {
            fprintf(stderr, "[zelda3d_dbg_input] AllGameInputBlocked changed: %d -> %d (pad fill %s)\n",
                    sLastBlocked, blocked, blocked ? "SKIPPED" : "resumed");
            sLastBlocked = blocked;
        }
    }

    if (AllGameInputBlocked()) {
        return;
    }

    mPads = pad;

    for (size_t i = 0; i < mPorts.size(); i++) {
        const std::shared_ptr<Ship::Controller> controller = mPorts[i]->GetConnectedController();

        if (controller != nullptr) {
            controller->ReadToPad(&pad[i]);
        }
    }

    // Shared scripted-input seam: OR-mix any synthetic pad state (headless FIFO poller /
    // per-game REPL / test) into port 0's real N64 pad. No-op unless explicitly enabled, so
    // physical input is untouched by default. This is the game-agnostic input path used by
    // both OoT (zelda3d) and MM (2s2h). See ship/controller/scripted/ScriptedInput.h.
    Ship_ScriptedInput_MixInto(&pad[0]);
}
} // namespace LUS
