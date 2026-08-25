#include "frontend_timing.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "core/core.h"
#include "frame_watchdog.h"
#include "libretro.h"
#include "libretro_frontend.h"
#include "repl_protocol.h"

extern "C" int soh3d_draw_index;

namespace HarnessFrontend {

void HandleRun(std::istringstream& arguments) {
    std::string frameCountText;
    if (!(arguments >> frameCountText)) {
        HarnessRepl::PrintErr("run: usage: run <N>");
        return;
    }
    const auto frameCount = HarnessRepl::ParseNum(frameCountText);
    if (!frameCount) {
        HarnessRepl::PrintErr("run: bad N");
        return;
    }

    uint64_t completed = 0;
    const bool trace = std::getenv("SOH3D_HARNESS_TRACE_FRAMES") != nullptr;
    auto lastTrace = std::chrono::steady_clock::now();
    for (uint64_t index = 0; index < *frameCount; ++index) {
        if (QuitRequested()) {
            break;
        }
        {
            HarnessWatchdog::Frame watchdog("HandleRun/retro_run");
            soh3d_draw_index = 0;
            retro_run();
        }
        ++completed;
        if (trace && (completed % 30) == 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTrace).count();
            std::fprintf(stderr, "[harness] run: %llu / %llu (last 30 frames = %lld ms)\n",
                         static_cast<unsigned long long>(completed), static_cast<unsigned long long>(*frameCount),
                         static_cast<long long>(milliseconds));
            lastTrace = now;
        }
    }
    std::printf("ok run %llu\n", static_cast<unsigned long long>(completed));
}

bool HandleTimingCommand(const std::string& command, std::istringstream& arguments) {
    if (command == "az_ticks") {
        const auto ticks = Core::System::GetInstance().CoreTiming().GetGlobalTicks();
        std::printf("ok az_ticks %lld\n", static_cast<long long>(ticks));
        return true;
    }
    if (command != "az_run_until") {
        return false;
    }

    std::string targetText;
    if (!(arguments >> targetText)) {
        HarnessRepl::PrintErr("az_run_until: usage: az_run_until <ticks>");
        return true;
    }
    const auto target = HarnessRepl::ParseNum(targetText);
    if (!target) {
        HarnessRepl::PrintErr("az_run_until: bad ticks");
        return true;
    }

    auto& system = Core::System::GetInstance();
    uint64_t frames = 0;
    constexpr uint64_t kMaxFrames = 100000;
    while (system.CoreTiming().GetGlobalTicks() < static_cast<s64>(*target) && frames < kMaxFrames &&
           !QuitRequested()) {
        {
            HarnessWatchdog::Frame watchdog("az_run_until/retro_run");
            retro_run();
        }
        ++frames;
    }
    const auto finalTicks = system.CoreTiming().GetGlobalTicks();
    std::printf("ok az_run_until frames=%llu final_ticks=%lld target=%lld\n", static_cast<unsigned long long>(frames),
                static_cast<long long>(finalTicks), static_cast<long long>(*target));
    return true;
}

} // namespace HarnessFrontend
