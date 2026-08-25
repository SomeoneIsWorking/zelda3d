#include "soh_player_control_commands.h"

#include <cstdio>

#include "repl_protocol.h"
#include "soh_input_state.h"
#include "soh_player_state.h"
#include "soh_runtime.h"

namespace HarnessSohPlayerControl {
namespace {

bool RequireBooted(const char* errorMessage) {
    if (HarnessSohRuntime::IsBooted()) {
        return true;
    }
    HarnessRepl::PrintErr(errorMessage);
    return false;
}

bool HandleTeleport(std::istringstream& arguments) {
    std::string xText;
    std::string yText;
    std::string zText;
    std::string yawText;
    if (!(arguments >> xText) || !(arguments >> yText) || !(arguments >> zText)) {
        HarnessRepl::PrintErr("soh_tp: usage: soh_tp <x> <y> <z> [yaw_s16]");
        return true;
    }
    const float x = std::stof(xText);
    const float y = std::stof(yText);
    const float z = std::stof(zText);
    if (!RequireBooted("soh_tp: run soh_boot first")) {
        return true;
    }
    if (!SohState_TeleportPlayer(x, y, z)) {
        HarnessRepl::PrintErr("soh_tp: no player");
        return true;
    }

    if (!(arguments >> yawText)) {
        std::printf("ok soh_tp %.2f %.2f %.2f\n", x, y, z);
        return true;
    }
    const auto yaw = HarnessRepl::ParseNum(yawText);
    if (!yaw) {
        HarnessRepl::PrintErr("soh_tp: bad yaw");
        return true;
    }
    if (!SohState_SetPlayerYaw(static_cast<int>(*yaw))) {
        HarnessRepl::PrintErr("soh_tp: no player (yaw)");
        return true;
    }
    std::printf("ok soh_tp %.2f %.2f %.2f yaw=%d\n", x, y, z, static_cast<int>(*yaw));
    return true;
}

bool HandleInput(std::istringstream& arguments) {
    std::string buttonText;
    std::string stickXText;
    std::string stickYText;
    if (!(arguments >> buttonText)) {
        HarnessRepl::PrintErr("soh_input: usage: soh_input <button-mask> [stickX] [stickY]");
        return true;
    }
    const auto buttons = HarnessRepl::ParseNum(buttonText);
    if (!buttons) {
        HarnessRepl::PrintErr("soh_input: bad mask");
        return true;
    }

    int stickX = 0;
    int stickY = 0;
    if (arguments >> stickXText) {
        if (const auto value = HarnessRepl::ParseNum(stickXText)) {
            stickX = static_cast<int>(*value);
        }
    }
    if (arguments >> stickYText) {
        if (const auto value = HarnessRepl::ParseNum(stickYText)) {
            stickY = static_cast<int>(*value);
        }
    }
    if (!RequireBooted("soh_input: run soh_boot first")) {
        return true;
    }

    const auto buttonMask = static_cast<unsigned int>(*buttons & 0xFFFF);
    if (!SohState_SetInput(buttonMask, stickX, stickY)) {
        HarnessRepl::PrintErr("soh_input: no playstate — soh_step until Play is up first");
        return true;
    }
    std::printf("ok soh_input 0x%04x stick=(%d,%d)\n", buttonMask, stickX, stickY);
    return true;
}

} // namespace

bool HandleCommand(const std::string& command, std::istringstream& arguments) {
    if (command == "soh_tp") {
        return HandleTeleport(arguments);
    }
    if (command == "soh_input") {
        return HandleInput(arguments);
    }
    return false;
}

} // namespace HarnessSohPlayerControl
