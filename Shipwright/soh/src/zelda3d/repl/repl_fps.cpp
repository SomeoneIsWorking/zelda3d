#include "repl_fps.h"

#include <ctime>

namespace {

struct FpsState {
    timespec samples[128] = {};
    int head = 0;
    int count = 0;
};

FpsState sFps;

} // namespace

namespace Zelda3D::Repl {

void TickFps() {
    clock_gettime(CLOCK_MONOTONIC, &sFps.samples[sFps.head]);
    sFps.head = (sFps.head + 1) & 127;
    if (sFps.count < 128) {
        ++sFps.count;
    }
}

void ResetFps() {
    sFps = {};
}

} // namespace Zelda3D::Repl

extern "C" double Zelda3D_ReplLogicFpsWindow(void) {
    if (sFps.count < 2) {
        return 0.0;
    }
    const timespec& newest = sFps.samples[(sFps.head + 127) & 127];
    const timespec& oldest = sFps.samples[(sFps.head - sFps.count + 128) & 127];
    return (newest.tv_sec - oldest.tv_sec) + (newest.tv_nsec - oldest.tv_nsec) * 1e-9;
}

extern "C" int Zelda3D_ReplLogicFpsSamples(void) {
    return sFps.count;
}

extern "C" double Zelda3D_ReplLogicFps(void) {
    const double window = Zelda3D_ReplLogicFpsWindow();
    return window > 0.0 ? (sFps.count - 1) / window : 0.0;
}
