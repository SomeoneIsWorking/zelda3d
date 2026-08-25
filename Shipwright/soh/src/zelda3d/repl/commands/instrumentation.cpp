#include "instrumentation.h"

#include "cvar_control.h"
#include "diagnostic_logging.h"
#include "model_submit_trace.h"
#include "pose_scan.h"

bool Zelda3D_InstrumentationReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    (void)play;
    return Zelda3D_DiagnosticLoggingReplCommand(command, line, outPath) ||
           Zelda3D_ModelSubmitTraceReplCommand(command, line, outPath) ||
           Zelda3D_PoseScanReplCommand(command, line, outPath) ||
           Zelda3D_CVarControlReplCommand(command, line, outPath);
}
