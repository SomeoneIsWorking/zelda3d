#include "2s2h/zelda3d/repl/mm3d_framing_repl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int sCameraActive = 0;
static float sCameraYawDegrees = 0.0f;
static float sCameraDistance = 180.0f;
static float sCameraHeight = 60.0f;

void Zelda3D_MmFramingReplReset(void) {
    sCameraActive = 0;
    sCameraYawDegrees = 0.0f;
    sCameraDistance = 180.0f;
    sCameraHeight = 60.0f;
}

void Zelda3D_MmFramingReplApply(PlayState* play) {
    if (!sCameraActive || (play == NULL)) {
        return;
    }
    Camera* camera = GET_ACTIVE_CAM(play);
    if (camera == NULL) {
        return;
    }
    Vec3f focus;
    if (camera->focalActor != NULL) {
        focus = camera->focalActor->world.pos;
    } else {
        Player* player = GET_PLAYER(play);
        if (player == NULL) {
            return;
        }
        focus = player->actor.world.pos;
    }
    float yawRadians = sCameraYawDegrees * (3.14159265358979f / 180.0f);
    Vec3f at = { focus.x, focus.y + sCameraHeight, focus.z };
    Vec3f eye = {
        at.x + sinf(yawRadians) * sCameraDistance,
        at.y,
        at.z + cosf(yawRadians) * sCameraDistance,
    };
    camera->at = at;
    camera->eye = eye;
    camera->eyeNext = eye;
    camera->up.x = 0.0f;
    camera->up.y = 1.0f;
    camera->up.z = 0.0f;
}

static void Zelda3D_MmFramingTeleport(PlayState* play, Zelda3DMmReplArgs* args, Zelda3DMmReplReply reply, void* user) {
    Player* player = (play != NULL) ? GET_PLAYER(play) : NULL;
    float x;
    float y;
    float z;
    if ((player == NULL) || !Zelda3D_MmReplParseFloat(args, &x) || !Zelda3D_MmReplParseFloat(args, &y) ||
        !Zelda3D_MmReplParseFloat(args, &z) || !Zelda3D_MmReplArgsEnd(args)) {
        reply("usage: tp <x> <y> <z>", user);
        return;
    }
    player->actor.world.pos.x = x;
    player->actor.world.pos.y = y;
    player->actor.world.pos.z = z;
    player->actor.prevPos = player->actor.world.pos;
    char output[128];
    snprintf(output, sizeof(output), "tp -> (%.0f,%.0f,%.0f)", x, y, z);
    reply(output, user);
}

static void Zelda3D_MmFramingTurn(PlayState* play, Zelda3DMmReplArgs* args, Zelda3DMmReplReply reply, void* user) {
    Player* player = (play != NULL) ? GET_PLAYER(play) : NULL;
    float degrees;
    if ((player == NULL) || !Zelda3D_MmReplParseFloat(args, &degrees) || !Zelda3D_MmReplArgsEnd(args)) {
        reply("usage: turn <deg>", user);
        return;
    }
    s16 yaw = (s16)(degrees * 182.0444f);
    player->actor.shape.rot.y = yaw;
    player->actor.world.rot.y = yaw;
    char output[64];
    snprintf(output, sizeof(output), "turn -> %.0f deg (yaw=%d)", degrees, yaw);
    reply(output, user);
}

static void Zelda3D_MmFramingCamera(Zelda3DMmReplArgs* args, Zelda3DMmReplReply reply, void* user) {
    Zelda3DMmReplArgs probe = *args;
    char token[16];
    if (Zelda3D_MmReplNextToken(&probe, token, sizeof(token)) && Zelda3D_MmReplArgsEnd(&probe) &&
        ((strcmp(token, "off") == 0) || (strcmp(token, "release") == 0))) {
        sCameraActive = 0;
        reply("cam released (game camera resumed)", user);
        return;
    }

    float yaw;
    float distance = 180.0f;
    float height = 60.0f;
    if (!Zelda3D_MmReplParseFloat(args, &yaw)) {
        reply("usage: cam <yawDeg> [dist] [height] | cam off", user);
        return;
    }
    int valueCount = 1;
    if (!Zelda3D_MmReplArgsEnd(args)) {
        if (!Zelda3D_MmReplParseFloat(args, &distance)) {
            reply("usage: cam <yawDeg> [dist] [height] | cam off", user);
            return;
        }
        valueCount = 2;
    }
    if (!Zelda3D_MmReplArgsEnd(args)) {
        if (!Zelda3D_MmReplParseFloat(args, &height)) {
            reply("usage: cam <yawDeg> [dist] [height] | cam off", user);
            return;
        }
        valueCount = 3;
    }
    if (!Zelda3D_MmReplArgsEnd(args)) {
        reply("usage: cam <yawDeg> [dist] [height] | cam off", user);
        return;
    }

    sCameraYawDegrees = yaw;
    if (valueCount >= 2) {
        sCameraDistance = distance;
    }
    if (valueCount >= 3) {
        sCameraHeight = height;
    }
    sCameraActive = 1;
    char output[128];
    snprintf(output, sizeof(output), "cam yaw=%.1f dist=%.1f h=%.1f (persistent)", sCameraYawDegrees, sCameraDistance,
             sCameraHeight);
    reply(output, user);
}

int Zelda3D_MmFramingReplDispatch(PlayState* play, const char* command, Zelda3DMmReplReply reply, void* user) {
    Zelda3DMmReplArgs args;
    if (Zelda3D_MmReplMatch(command, "tp", &args)) {
        Zelda3D_MmFramingTeleport(play, &args, reply, user);
        return 1;
    }
    if (Zelda3D_MmReplMatch(command, "turn", &args)) {
        Zelda3D_MmFramingTurn(play, &args, reply, user);
        return 1;
    }
    if (Zelda3D_MmReplMatch(command, "cam", &args)) {
        Zelda3D_MmFramingCamera(&args, reply, user);
        return 1;
    }
    return 0;
}
