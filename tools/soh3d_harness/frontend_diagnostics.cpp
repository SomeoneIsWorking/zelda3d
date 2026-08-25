#include "frontend_diagnostics.h"

#include <cstdio>
#include <cstdint>

#include "frontend_input.h"
#include "frontend_presentation.h"
#include "soh_capture_bridge.h"
#include "soh_runtime.h"

namespace HarnessFrontend {

void HandleDiag(std::istringstream&) {
    std::printf("ok mask=0x%08x polls=%llu ids_seen=0x%08x\n"
                "  az:  booted=1 w=%u h=%u pitch=%zu dirty=%d\n"
                "  soh: booted=%d captureW=%u captureH=%u pending=%d\n"
                "  fb0: attempts=%d inRange=%d hasColor=%d lastW=%u lastH=%u\n"
                "ok end\n",
                InputMask(), static_cast<unsigned long long>(InputPollCount()), InputIdsSeen(), OracleWidth(),
                OracleHeight(), OraclePitch(), OracleDirty() ? 1 : 0, HarnessSohRuntime::IsBooted() ? 1 : 0,
                gSoh3dCaptureW, gSoh3dCaptureH, gSoh3dCapturePending, static_cast<int>(gSoh3dFb0LastCaptureAttempt),
                static_cast<int>(gSoh3dFb0LastInRange), static_cast<int>(gSoh3dFb0LastHasColor),
                static_cast<unsigned>(gSoh3dFb0LastW), static_cast<unsigned>(gSoh3dFb0LastH));
}

} // namespace HarnessFrontend
