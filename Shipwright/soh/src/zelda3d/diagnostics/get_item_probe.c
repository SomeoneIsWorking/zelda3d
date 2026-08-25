#include "get_item_probe.h"
#include "functions/math.h"
#include "functions/rendering.h"

#include <stdlib.h>

int gZelda3dSpawnGi = -2;
float gZelda3dGiDisp = 0.2f;

void Zelda3D_DebugDrawGetItem(PlayState* play) {
    int gid = gZelda3dSpawnGi;
    Player* player;
    s16 yaw;
    float x, y, z;

    if (gid == -2) {
        const char* value = getenv("ZELDA3D_SPAWNGI");
        gZelda3dSpawnGi = (value != NULL && value[0] != '\0') ? atoi(value) : -1;
        gid = gZelda3dSpawnGi;
    }
    if (gid < 0) {
        return;
    }

    player = GET_PLAYER(play);
    yaw = player->actor.shape.rot.y;
    x = player->actor.world.pos.x + 3.3f * Math_SinS(yaw);
    z = player->actor.world.pos.z + 3.3f * Math_CosS(yaw);
    y = player->actor.world.pos.y + 14.0f;
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_RotateZYX(0, play->gameplayFrames * 1000, 0, MTXMODE_APPLY);
    Matrix_Scale(gZelda3dGiDisp, gZelda3dGiDisp, gZelda3dGiDisp, MTXMODE_APPLY);
    GetItem_Draw(play, (s16)gid);
}
