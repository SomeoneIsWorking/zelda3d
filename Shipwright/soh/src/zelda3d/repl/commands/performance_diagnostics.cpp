#include "performance_diagnostics.h"

#include "../repl_fps.h"
#include "../zelda3d_repl.h"
#include "soh/host/frame_timing.h"

#include <cstring>

bool Zelda3D_PerformanceDiagnosticsReplCommand(const char* command, const char* outPath) {
    if (std::strcmp(command, "fps") != 0) {
        return false;
    }

    Zelda3D_ReplReply(outPath,
                      "logicFps=%.1f (n=%d over %.2fs) presentFps=%.1f "
                      "R_UPDATE_RATE=%d interpTarget=%d",
                      Zelda3D_ReplLogicFps(), Zelda3D_ReplLogicFpsSamples(), Zelda3D_ReplLogicFpsWindow(),
                      Zelda3D_PresentFps(), R_UPDATE_RATE, static_cast<int>(OTRGlobals_GetInterpolationFPS()));
    return true;
}
