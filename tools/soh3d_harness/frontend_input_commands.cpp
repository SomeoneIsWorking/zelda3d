#include "frontend_input_commands.h"

#include <cstdint>
#include <cstdio>

#include "frontend_input.h"
#include "repl_protocol.h"

namespace HarnessFrontendInput {

bool HandleCommand(const std::string& command, std::istringstream& arguments) {
    if (command != "analog") {
        return false;
    }

    std::string leftXText;
    std::string leftYText;
    std::string rightXText;
    std::string rightYText;
    if (!(arguments >> leftXText) || !(arguments >> leftYText)) {
        HarnessRepl::PrintErr("analog: usage: analog <lx> <ly> [rx] [ry]  (s16 range -32768..32767)");
        return true;
    }
    const auto requestedLeftX = HarnessRepl::ParseNum(leftXText);
    const auto requestedLeftY = HarnessRepl::ParseNum(leftYText);
    if (!requestedLeftX || !requestedLeftY) {
        HarnessRepl::PrintErr("analog: bad number");
        return true;
    }

    int16_t rightX = 0;
    int16_t rightY = 0;
    if (arguments >> rightXText) {
        if (const auto value = HarnessRepl::ParseNum(rightXText)) {
            rightX = static_cast<int16_t>(*value);
        }
    }
    if (arguments >> rightYText) {
        if (const auto value = HarnessRepl::ParseNum(rightYText)) {
            rightY = static_cast<int16_t>(*value);
        }
    }

    HarnessFrontend::SetAnalog(static_cast<int16_t>(*requestedLeftX), static_cast<int16_t>(*requestedLeftY), rightX,
                               rightY);
    int16_t leftX = 0;
    int16_t leftY = 0;
    HarnessFrontend::GetAnalog(&leftX, &leftY, &rightX, &rightY);
    std::printf("ok analog L=(%d,%d) R=(%d,%d)\n", static_cast<int>(leftX), static_cast<int>(leftY),
                static_cast<int>(rightX), static_cast<int>(rightY));
    return true;
}

} // namespace HarnessFrontendInput
