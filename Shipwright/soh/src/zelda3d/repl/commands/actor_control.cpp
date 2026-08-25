#include "actor_control.h"

#include "../../diagnostics/actor_selection.h"
#include "actor_targeting.h"
#include "actor_transform_control.h"
#include "boss_fd.h"
#include "volvagia_ball_diagnostics.h"

bool Zelda3D_ActorControlReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    return Zelda3D_ActorTargetingReplCommand(play, command, line, outPath) ||
           Zelda3D_BossFdReplCommand(play, gZelda3dSelActor, command, line, outPath) ||
           Zelda3D_VolvagiaBallDiagnosticsReplCommand(play, command, line, outPath) ||
           Zelda3D_ActorTransformControlReplCommand(play, command, line, outPath);
}
