#include "model_control.h"

#include "actor_model_overrides.h"
#include "actor_spawn_control.h"
#include "model_auto_diagnostics.h"
#include "model_joint_diagnostics.h"
#include "model_table_control.h"
#include "render_debug_control.h"

bool Zelda3D_ModelControlReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    return Zelda3D_ModelAutoDiagnosticsReplCommand(command, line, outPath) ||
           Zelda3D_ModelJointDiagnosticsReplCommand(play, command, line, outPath) ||
           Zelda3D_ModelTableReplCommand(command, line, outPath) ||
           Zelda3D_ActorSpawnReplCommand(play, command, line, outPath) ||
           Zelda3D_ActorModelOverridesReplCommand(command, line, outPath) ||
           Zelda3D_RenderDebugReplCommand(command, line, outPath);
}
