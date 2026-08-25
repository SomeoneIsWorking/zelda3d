#include "frame_timing.h"

#include "soh/OTRGlobals.h"

#include <chrono>
#include <cmath>
#include <ctime>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

timespec sPresentRing[256];
int sPresentHead = 0;
int sPresentCount = 0;

} // namespace

void Zelda3D_RecordPresentedFrame() {
    clock_gettime(CLOCK_MONOTONIC, &sPresentRing[sPresentHead]);
    sPresentHead = (sPresentHead + 1) & 255;
    if (sPresentCount < 256) {
        ++sPresentCount;
    }
}

#ifdef _WIN32
extern "C" uint64_t GetFrequency(void) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart;
}

extern "C" uint64_t GetPerfCounter(void) {
    LARGE_INTEGER ticks;
    QueryPerformanceCounter(&ticks);
    return ticks.QuadPart;
}
#else
extern "C" uint64_t GetFrequency(void) {
    return 1000;
}

extern "C" uint64_t GetPerfCounter(void) {
    timespec monotime;
    clock_gettime(CLOCK_MONOTONIC, &monotime);
    const uint64_t remainingMs = monotime.tv_nsec / 1000000;
    return monotime.tv_sec * 1000 + remainingMs;
}
#endif

extern "C" uint64_t GetUnixTimestamp(void) {
    const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count());
}

extern "C" double Zelda3D_PresentFps(void) {
    if (sPresentCount < 2) {
        return 0.0;
    }

    const timespec& newest = sPresentRing[(sPresentHead + 255) & 255];
    const timespec& oldest = sPresentRing[(sPresentHead - sPresentCount + 256) & 255];
    const double windowSeconds = (newest.tv_sec - oldest.tv_sec) + (newest.tv_nsec - oldest.tv_nsec) * 1e-9;
    return windowSeconds > 0.0 ? (sPresentCount - 1) / windowSeconds : 0.0;
}

extern "C" uint32_t OTRGlobals_GetInterpolationFPS(void) {
    return OTRGlobals::Instance->GetInterpolationFPS();
}

extern "C" uint32_t Ship_GetInterpolationFrameCount(void) {
    return static_cast<uint32_t>(std::ceil(static_cast<float>(OTRGlobals::Instance->GetInterpolationFPS()) / 20.0f));
}
