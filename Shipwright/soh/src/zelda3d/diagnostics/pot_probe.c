#include "pot_probe.h"
#include "functions/actors.h"
#include "functions/math.h"

#include <stdlib.h>

void Zelda3D_DebugDrawPot(PlayState* play) {
    const char* value = getenv("ZELDA3D_SPAWNPOT");
    static unsigned char spawned = 0;

    if (value != NULL && value[0] == '1' && !spawned) {
        Player* player = GET_PLAYER(play);
        s16 yaw = player->actor.shape.rot.y + 0x4000;
        float x = player->actor.world.pos.x + 60.0f * Math_SinS(yaw);
        float z = player->actor.world.pos.z + 60.0f * Math_CosS(yaw);
        Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_TSUBO, x, player->actor.world.pos.y, z, 0, 0, 0, 0);
        spawned = 1;
    }
}
