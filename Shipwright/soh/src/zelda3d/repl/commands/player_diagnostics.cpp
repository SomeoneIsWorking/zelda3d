#include "player_diagnostics.h"
#include "functions/collision.h"

#include <cstring>

#include "../../player/player_animation_policy.h"
#include "../../player/player_ground_diagnostics.h"
#include "../zelda3d_repl.h"

namespace {

void HandlePositionInfo(PlayState* play, const char* outPath) {
    Player* player = GET_PLAYER(play);
    Camera* camera = GET_ACTIVE_CAM(play);
    Zelda3D_ReplReply(outPath,
                      "scene=0x%x link=(%.0f,%.0f,%.0f) yaw=%d | cam eye=(%.0f,%.0f,%.0f) at=(%.0f,%.0f,%.0f) | "
                      "focus=(%.0f,%.0f,%.0f)",
                      play->sceneNum, player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                      player->actor.shape.rot.y, camera->eye.x, camera->eye.y, camera->eye.z, camera->at.x,
                      camera->at.y, camera->at.z, player->actor.focus.pos.x, player->actor.focus.pos.y,
                      player->actor.focus.pos.z);
}

void HandleCameraMode(PlayState* play, const char* outPath) {
    Camera* camera = GET_ACTIVE_CAM(play);
    const char* functionName = "(none)";
    s16 functionIndex = Zelda3D_CameraActiveFuncIdx(camera, &functionName);
    if (camera != nullptr) {
        Zelda3D_ReplReply(outPath,
                          "cammode scene=0x%x setting=%d mode=%d camDataIdx=%d -> funcIdx=%d (%s) | "
                          "roll=%d fov=%.1f",
                          play->sceneNum, camera->setting, camera->mode, camera->camDataIdx, functionIndex,
                          functionName, camera->roll, camera->fov);
    } else {
        Zelda3D_ReplReply(outPath, "cammode: no active camera");
    }
}

void HandleClimbInfo(PlayState* play, const char* outPath) {
    Player* player = GET_PLAYER(play);
    CollisionPoly* wallPoly = player->actor.wallPoly;
    s16 yawDifference = static_cast<s16>(player->actor.shape.rot.y - player->actor.wallYaw);
    if (wallPoly != nullptr) {
        s16 climbFlags = func_80041DB8(&play->colCtx, wallPoly, player->actor.wallBgId);
        Zelda3D_ReplReply(outPath,
                          "climbinfo bgF=0x%x st1=0x%x st2=0x%x pos=(%.0f,%.0f,%.0f) | wall n=(%.3f,%.3f,%.3f) "
                          "|ny|raw=%d climbFlags=%d wallYaw=%d shapeYaw=%d yawDiff=%d distWall=%.1f yDistLedge=%.1f "
                          "ledgeType=%d",
                          player->actor.bgCheckFlags, player->stateFlags1, player->stateFlags2,
                          player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                          COLPOLY_GET_NORMAL(wallPoly->normal.x), COLPOLY_GET_NORMAL(wallPoly->normal.y),
                          COLPOLY_GET_NORMAL(wallPoly->normal.z), static_cast<int>(ABS(wallPoly->normal.y)),
                          static_cast<int>(climbFlags), player->actor.wallYaw, player->actor.shape.rot.y,
                          static_cast<int>(yawDifference), player->distToInteractWall, player->yDistToLedge,
                          player->ledgeClimbType);
    } else {
        Zelda3D_ReplReply(outPath,
                          "climbinfo bgF=0x%x st1=0x%x st2=0x%x pos=(%.0f,%.0f,%.0f) | NO wallPoly (not touching a "
                          "wall) shapeYaw=%d yDistLedge=%.1f ledgeType=%d",
                          player->actor.bgCheckFlags, player->stateFlags1, player->stateFlags2,
                          player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                          player->actor.shape.rot.y, player->yDistToLedge, player->ledgeClimbType);
    }
}

void HandleLinkAnimationState(PlayState* play, const char* outPath) {
    Player* player = GET_PLAYER(play);
    const char* baseOtr = reinterpret_cast<const char*>(player->skelAnime.animation);
    const char* baseCsab = baseOtr ? Zelda3D_ResolvePlayerCsab(baseOtr) : "(null)";
    baseCsab = Zelda3D_LinkWalkRunGate(baseCsab, player->actor.speedXZ);
    const char* upperOtr = reinterpret_cast<const char*>(player->upperSkelAnime.animation);
    const char* upperCsab = upperOtr ? Zelda3D_ResolvePlayerCsab(upperOtr) : "(none)";
    Zelda3D_ReplReply(outPath,
                      "base=%s f=%.1f/%.1f spd=%.2f morph=%.2f | upper=%s f=%.1f/%.1f morph=%.2f | "
                      "upperLimbRot=(%d,%d,%d) headRotY=%d | shapeY=%d yaw=%d focusY=%d speedXZ=%.2f st1=0x%x "
                      "sideWalkBlend=%.2f",
                      baseCsab ? baseCsab : "(unmapped)", player->skelAnime.curFrame, player->skelAnime.animLength,
                      player->skelAnime.playSpeed, player->skelAnime.morphWeight, upperCsab ? upperCsab : "(unmapped)",
                      player->upperSkelAnime.curFrame, player->upperSkelAnime.animLength,
                      player->upperSkelAnime.morphWeight, player->upperLimbRot.x, player->upperLimbRot.y,
                      player->upperLimbRot.z, player->headLimbRot.y, player->actor.shape.rot.y, player->yaw,
                      player->actor.focus.rot.y, player->actor.speedXZ, player->stateFlags1, player->unk_870);
}

void HandleLinkGround(PlayState* play, const char* outPath) {
    const char* csab = "(?)";
    float groundOffset = Zelda3D_LinkGroundDiag(play, &csab);
    Zelda3D_ReplReply(outPath, "linkground csab=%s groundOff=%.2f (model-local; grounds lowest vertex to actorY)", csab,
                      groundOffset);
}

} // namespace

bool Zelda3D_PlayerDiagnosticsReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    (void)line;
    if (std::strcmp(command, "posinfo") == 0) {
        HandlePositionInfo(play, outPath);
    } else if (std::strcmp(command, "cammode") == 0) {
        HandleCameraMode(play, outPath);
    } else if (std::strcmp(command, "climbinfo") == 0) {
        HandleClimbInfo(play, outPath);
    } else if (std::strcmp(command, "linkanimstate") == 0) {
        HandleLinkAnimationState(play, outPath);
    } else if (std::strcmp(command, "linkground") == 0) {
        HandleLinkGround(play, outPath);
    } else {
        return false;
    }
    return true;
}
