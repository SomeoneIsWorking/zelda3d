#include "player_movement_control.h"
#include "functions/math.h"

#include <cstdio>
#include <cstring>

#include "../../control/player_state_control.h"
#include "../../input/zelda3d_input.h"
#include "../../scene/cinematic_camera_state.h"
#include "../zelda3d_repl.h"

namespace {

bool HandleMove(PlayState* play, const char* line, const char* outPath) {
    float distance;
    if (std::sscanf(line, "%*s %f", &distance) != 1) {
        return false;
    }

    Player* player = GET_PLAYER(play);
    s16 yaw = player->actor.shape.rot.y;
    player->actor.world.pos.x += distance * Math_SinS(yaw);
    player->actor.world.pos.z += distance * Math_CosS(yaw);
    player->actor.prevPos = player->actor.world.pos;
    Zelda3D_ReplReply(outPath, "move %.0f -> (%.0f,%.0f,%.0f)", distance, player->actor.world.pos.x,
                      player->actor.world.pos.y, player->actor.world.pos.z);
    return true;
}

bool HandleGameCamera(const char* line, const char* outPath) {
    int enabled;
    if (std::sscanf(line, "%*s %i", &enabled) != 1) {
        return false;
    }

    gZelda3dGCam = enabled ? 1 : 0;
    Zelda3D_ReplReply(outPath, "gcam=%d (force game camera behind Link for walkhold-driven locomotion)", gZelda3dGCam);
    return true;
}

bool HandleTurn(PlayState* play, const char* line, const char* outPath) {
    float degrees;
    if (std::sscanf(line, "%*s %f", &degrees) != 1) {
        return false;
    }

    Player* player = GET_PLAYER(play);
    s16 yaw = static_cast<s16>(degrees * 182.0444f); // deg -> binang
    player->actor.shape.rot.y = yaw;
    player->actor.world.rot.y = yaw;
    Zelda3D_ReplReply(outPath, "turn -> %.0f deg (yaw=%d)", degrees, yaw);
    return true;
}

bool HandleFloorTeleport(PlayState* play, const char* line, const char* outPath) {
    float x;
    float z;
    if (std::sscanf(line, "%*s %f %f", &x, &z) != 2) {
        return false;
    }

    Player* player = GET_PLAYER(play);
    float yawDegrees;
    s32 setYaw = std::sscanf(line, "%*s %*f %*f %f", &yawDegrees) == 1;
    s16 yaw = setYaw ? static_cast<s16>(yawDegrees / 360.0f * 65536.0f) : 0;
    f32 y = Zelda3D_PlayerForceTeleport(player, play, x, z, yaw, setYaw);
    Zelda3D_ReplReply(outPath, "tpf -> (%.0f,%.1f,%.0f) yaw=%d%s", x, y, z, player->actor.shape.rot.y,
                      setYaw ? " (aimed)" : "");
    return true;
}

} // namespace

bool Zelda3D_PlayerMovementControlReplCommand(PlayState* play, const char* command, const char* line,
                                              const char* outPath) {
    if (std::strcmp(command, "move") == 0) {
        return HandleMove(play, line, outPath);
    }
    if (std::strcmp(command, "gcam") == 0) {
        return HandleGameCamera(line, outPath);
    }
    if (std::strcmp(command, "walkhold") == 0) {
        Zelda3D_Input_HandleWalkHoldCmd(line, outPath);
        return true;
    }
    if (std::strcmp(command, "btnhold") == 0) {
        Zelda3D_Input_HandleBtnHoldCmd(line, outPath);
        return true;
    }
    if (std::strcmp(command, "turn") == 0) {
        return HandleTurn(play, line, outPath);
    }
    if (std::strcmp(command, "tpf") == 0) {
        return HandleFloorTeleport(play, line, outPath);
    }
    return false;
}
