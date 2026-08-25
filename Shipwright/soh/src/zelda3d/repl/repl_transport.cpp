#include "repl_transport.h"

#include "repl_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

struct TransportState {
    int descriptor = -2; // -2 uninitialized, -1 disabled
    char responsePath[1100] = {};
    char input[8192] = {};
    int inputLength = 0;
};

TransportState sTransport;

void InitializeTransport() {
    const char* path = std::getenv("ZELDA3D_REPL");
    if (path == nullptr || path[0] == '\0') {
        sTransport.descriptor = -1;
        return;
    }

    mkfifo(path, 0666); // EEXIST is expected for a persistent control path.
    sTransport.descriptor = open(path, O_RDWR | O_NONBLOCK);
    std::snprintf(sTransport.responsePath, sizeof(sTransport.responsePath), "%s.out", path);
    if (sTransport.descriptor >= 0) {
        FILE* response = std::fopen(sTransport.responsePath, "w");
        if (response != nullptr) {
            std::fprintf(response, "SOH3D REPL ready (fifo=%s)\n", path);
            std::fclose(response);
        }
    }
}

void ReadAvailableInput() {
    for (;;) {
        if (sTransport.inputLength >= static_cast<int>(sizeof(sTransport.input)) - 1) {
            sTransport.inputLength = 0;
        }
        const ssize_t count = read(sTransport.descriptor, sTransport.input + sTransport.inputLength,
                                   sizeof(sTransport.input) - 1 - sTransport.inputLength);
        if (count <= 0) {
            break;
        }
        sTransport.inputLength += static_cast<int>(count);
    }
    sTransport.input[sTransport.inputLength] = '\0';
}

void DispatchCompleteLines(PlayState* play) {
    char* start = sTransport.input;
    char* newline = nullptr;
    while ((newline = std::strchr(start, '\n')) != nullptr) {
        *newline = '\0';
        Zelda3D_ReplDispatchCommand(play, start, sTransport.responsePath);
        start = newline + 1;
    }
    sTransport.inputLength = static_cast<int>(std::strlen(start));
    std::memmove(sTransport.input, start, sTransport.inputLength + 1);
}

} // namespace

namespace Zelda3D::Repl {

void PollTransport(PlayState* play) {
    if (sTransport.descriptor == -2) {
        InitializeTransport();
    }
    if (sTransport.descriptor < 0) {
        return;
    }
    ReadAvailableInput();
    DispatchCompleteLines(play);
}

void ResetTransport() {
    if (sTransport.descriptor >= 0) {
        close(sTransport.descriptor);
    }
    sTransport = {};
}

} // namespace Zelda3D::Repl
