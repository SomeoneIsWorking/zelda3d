#include "frontend_input_commands.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#include "frontend_input.h"
#include "repl_protocol.h"

namespace HarnessFrontendInput {

namespace {

bool ParseInt16(const std::string& text, int16_t* value) {
    char* end = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &end, 0);
    if (end == text.c_str() || *end != '\0' || parsed < std::numeric_limits<int16_t>::min() ||
        parsed > std::numeric_limits<int16_t>::max()) {
        return false;
    }
    *value = static_cast<int16_t>(parsed);
    return true;
}

} // namespace

bool HandleCommand(const std::string& command, std::istringstream& arguments) {
    if (command == "pointer") {
        std::string xText;
        std::string yText;
        std::string pressedText;
        int16_t x = 0;
        int16_t y = 0;
        if (!(arguments >> xText >> yText >> pressedText) || !ParseInt16(xText, &x) || !ParseInt16(yText, &y)) {
            HarnessRepl::PrintErr("pointer: usage: pointer <x> <y> <pressed> (s16 coordinates, pressed 0|1)");
            return true;
        }
        const auto pressed = HarnessRepl::ParseNum(pressedText);
        if (!pressed || *pressed > 1) {
            HarnessRepl::PrintErr("pointer: pressed must be 0 or 1");
            return true;
        }
        HarnessFrontend::SetPointer(x, y, *pressed != 0);
        std::printf("ok pointer (%d,%d) pressed=%d\n", static_cast<int>(x), static_cast<int>(y), *pressed != 0 ? 1 : 0);
        return true;
    }
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
