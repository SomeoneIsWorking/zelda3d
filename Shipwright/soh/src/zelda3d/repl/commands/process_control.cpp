#include "process_control.h"

#include "frame_capture_control.h"
#include "performance_diagnostics.h"
#include "process_exit_control.h"

bool Zelda3D_ProcessControlReplCommand(PlayState*, const char* command, const char* line, const char* outPath) {
    return Zelda3D_PerformanceDiagnosticsReplCommand(command, outPath) ||
           Zelda3D_FrameCaptureControlReplCommand(command, line, outPath) ||
           Zelda3D_ProcessExitControlReplCommand(command, outPath);
}
