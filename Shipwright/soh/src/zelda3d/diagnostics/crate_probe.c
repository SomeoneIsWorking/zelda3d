#include "crate_probe.h"
#include "functions/actors.h"
#include "functions/math.h"

#include <stdio.h>
#include <stdlib.h>

void Zelda3D_DebugDrawKibako(PlayState* play) {
    const char* value = getenv("ZELDA3D_SPAWNKIBAKO");
    static unsigned char spawned = 0;

    if (value != NULL && value[0] == '1' && !spawned) {
        Player* player = GET_PLAYER(play);
        s16 yaw = player->actor.shape.rot.y;
        float x = player->actor.world.pos.x + 120.0f * Math_SinS(yaw);
        float z = player->actor.world.pos.z + 120.0f * Math_CosS(yaw);
        Actor* crate = Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_KIBAKO2, x, player->actor.world.pos.y, z, 0,
                                   player->actor.shape.rot.y, 0, 0);
        fprintf(stderr, "SOH3D: SPAWNKIBAKO Actor_Spawn(OBJ_KIBAKO2) -> %s\n",
                crate != NULL ? "OK" : "FAILED (object not in scene)");
        fflush(stdout);
        spawned = 1;
    }
}
