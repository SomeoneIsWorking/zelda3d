#include "repl_protocol.h"

#include <cstdio>
#include <cstdlib>

namespace HarnessRepl {

std::optional<uint64_t> ParseNum(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const unsigned long long number = std::strtoull(value.c_str(), &end, 0);
    if (end == value.c_str() || *end != '\0') {
        return std::nullopt;
    }
    return static_cast<uint64_t>(number);
}

void PrintErr(const char* message) {
    std::printf("err %s\n", message);
}

} // namespace HarnessRepl
