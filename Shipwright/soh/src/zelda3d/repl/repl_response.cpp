#include "zelda3d_repl.h"

#include <cstdarg>
#include <cstdio>

extern "C" void Zelda3D_ReplReply(const char* outPath, const char* fmt, ...) {
    // Multi-line dump replies such as `posescan dump` can exceed 8 KiB.
    char message[16384];
    va_list arguments;
    va_start(arguments, fmt);
    std::vsnprintf(message, sizeof(message), fmt, arguments);
    va_end(arguments);

    std::fprintf(stderr, "SOH3D REPL: %s\n", message);
    std::fflush(stdout);
    FILE* response = std::fopen(outPath, "a");
    if (response != nullptr) {
        std::fprintf(response, "%s\n", message);
        std::fclose(response);
    }
}
