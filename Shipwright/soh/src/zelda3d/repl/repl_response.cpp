#include "zelda3d_repl.h"

#include "../core/zelda3d_log.h"

#include <cstdarg>
#include <cstdio>

extern "C" void Zelda3D_ReplReply(const char* outPath, const char* fmt, ...) {
    // Multi-line dump replies such as `posescan dump` can exceed 8 KiB.
    char message[16384];
    va_list arguments;
    va_start(arguments, fmt);
    std::vsnprintf(message, sizeof(message), fmt, arguments);
    va_end(arguments);

    // One call, not one per line, and that is a deliberate exception to how the lifecycle reports are
    // written. This echoes the reply that is simultaneously appended verbatim to the response file, so
    // the terminal copy and the file copy have to be the same text; splitting a multi-line dump across
    // calls would give the two copies different shapes.
    Z3D_LOG_INFO(SOH_REPL, "%s", message);
    FILE* response = std::fopen(outPath, "a");
    if (response != nullptr) {
        std::fprintf(response, "%s\n", message);
        std::fclose(response);
    }
}
