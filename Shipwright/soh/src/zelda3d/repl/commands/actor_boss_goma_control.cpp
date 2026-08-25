#include "actor_boss_goma_control.h"

#include "../../behaviors/actor/boss_goma_bridge.h"
#include "../../diagnostics/actor_selection.h"
#include "../../render/actor_control_state.h"
#include "../zelda3d_repl.h"

#include <stdio.h>
#include <string.h>

bool Zelda3D_ActorBossGomaReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    (void)play;
    if (strcmp(command, "gohmaclimb") != 0) {
        return false;
    }

    char subcommand[16] = {};
    float climbY = -560.0f;
    int hold = 1;
    if (sscanf(line, "%*s %15s", subcommand) == 1 && strcmp(subcommand, "off") == 0) {
        Zelda3D_BossGomaForceClimb(nullptr, 0.0f, 0);
        Zelda3D_ReplReply(outPath, "gohmaclimb off (hold released, held=%d)", Zelda3D_BossGomaClimbHeld());
    } else {
        (void)sscanf(line, "%*s %f %d", &climbY, &hold);
        if (gZelda3dSelActor == nullptr) {
            Zelda3D_ReplReply(outPath, "gohmaclimb: no selection (asel 0x28 first)");
        } else if (!Zelda3D_BossGomaForceClimb(gZelda3dSelActor, climbY, hold)) {
            Zelda3D_ReplReply(outPath, "gohmaclimb: selection is not Boss_Goma (asel 0x28)");
        } else {
            Actor* actor = gZelda3dSelActor;
            sZelda3dActorPinPos = actor->world.pos;
            Zelda3D_ReplReply(outPath,
                              "gohmaclimb: climbing pos=(%.0f,%.0f,%.0f) hold=%d held=%d "
                              "(shape.rot.x will approach -16384; let frames pass then aaim/ainfo)",
                              actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, hold,
                              Zelda3D_BossGomaClimbHeld());
        }
    }
    return true;
}
