#include "collectible_probe.h"
#include "functions/actors.h"
#include "functions/math.h"

#include <stdio.h>
#include <stdlib.h>

void Zelda3D_DebugDrawDrop(PlayState* play) {
    const char* value = getenv("ZELDA3D_SPAWNDROP");
    static unsigned char spawned = 0;

    if (value != NULL && value[0] != '\0' && !spawned) {
        Player* player = GET_PLAYER(play);
        s16 yaw = player->actor.shape.rot.y;
        Vec3f pos = {
            player->actor.world.pos.x + 70.0f * Math_SinS(yaw),
            player->actor.world.pos.y + 20.0f,
            player->actor.world.pos.z + 70.0f * Math_CosS(yaw),
        };
        long dropId = strtol(value, NULL, 0);
        EnItem00* item = Item_DropCollectible(play, &pos, (s16)dropId);
        fprintf(stderr, "[Zelda3D #36] dropped id=%ld at (%.0f,%.0f,%.0f) -> %s\n", dropId, pos.x, pos.y, pos.z,
                item != NULL ? "OK" : "NULL");
        spawned = 1;
    }
}
