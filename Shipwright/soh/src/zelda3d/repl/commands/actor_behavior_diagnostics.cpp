#include "actor_behavior_diagnostics.h"

#include "actor_bone_diagnostics.h"
#include "actor_boss_goma_control.h"
#include "actor_cucco_control.h"
#include "actor_door_control.h"

bool Zelda3D_ActorBehaviorDiagnosticsReplCommand(PlayState* play, const char* command, const char* line,
                                                 const char* outPath) {
    return Zelda3D_ActorCuccoReplCommand(command, line, outPath) ||
           Zelda3D_ActorBoneDiagnosticsReplCommand(command, line, outPath) ||
           Zelda3D_ActorDoorReplCommand(command, line, outPath) ||
           Zelda3D_ActorBossGomaReplCommand(play, command, line, outPath);
}
