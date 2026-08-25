#include "lockstep_runner.h"

#include <cstdio>
#include <string>

#include "frame_watchdog.h"
#include "libretro.h"
#include "libretro_frontend.h"
#include "repl_protocol.h"
#include "soh_runtime.h"
#include "title_sync_runtime.h"

namespace HarnessLockstep {
namespace {

using FrameWatchdog = HarnessWatchdog::Frame;
using HarnessRepl::ParseNum;
using HarnessRepl::PrintErr;

void AdvanceOracleFrame() {
    FrameWatchdog watchdog("HandleStep/retro_run");
    retro_run();
}

} // namespace

void HandleStep(std::istringstream& arguments) {
    std::string countText;
    if (!(arguments >> countText)) {
        PrintErr("step: usage: step <N>");
        return;
    }
    const auto count = ParseNum(countText);
    if (!count) {
        PrintErr("step: bad N");
        return;
    }

    if (HarnessTitleSyncRuntime::EnsureArmed() == HarnessTitleSyncRuntime::ArmResult::Failed) {
        PrintErr("step: title-sync arm failed (see stderr above) -- not "
                 "stepping; fix the issue and retry, or run a manual "
                 "`loadstate`+`soh_boot` first for legacy passthrough "
                 "stepping of a different scene");
        return;
    }

    const bool syncActive = HarnessTitleSyncRuntime::IsActive();
    uint64_t completed = 0;
    for (uint64_t frame = 0; frame < *count; ++frame) {
        if (HarnessFrontend::QuitRequested()) {
            break;
        }
        if (syncActive) {
            if (HarnessSohRuntime::IsBooted()) {
                HarnessSohRuntime::AdvanceFrame("HandleStep/RunFrame");
                HarnessTitleSyncRuntime::AdvanceAfterSohFrame();
            }
        } else {
            AdvanceOracleFrame();
            if (HarnessSohRuntime::IsBooted()) {
                HarnessSohRuntime::AdvanceFrame("HandleStep/RunFrame");
            }
        }
        ++completed;
    }

    std::printf("ok step %llu %s%s\n", static_cast<unsigned long long>(completed),
                HarnessSohRuntime::IsBooted() ? "azahar+soh3d" : "azahar-only", HarnessTitleSyncRuntime::StatusTag());
}

} // namespace HarnessLockstep
