#include "volvagia_ball_diagnostics.h"

#include <cstdio>
#include <cstring>

#include "../../behaviors/actor/en_vb_ball_bridge.h"
#include "../../diagnostics/actor_selection.h"
#include "../../render/actor_control_state.h"
#include "../zelda3d_repl.h"
#include "overlays/actors/ovl_En_Vb_Ball/z_en_vb_ball.h"

namespace {

void SpawnVolvagiaBall(PlayState* play, const char* line, const char* outPath) {
    int params = -1;
    (void)std::sscanf(line, "%*s %i", &params);
    Actor* child = Zelda3D_EnVbBallSpawnDiagnostic(play, gZelda3dSelActor, params);
    if (child == nullptr) {
        Zelda3D_ReplReply(
            outPath, "vbball: scanned selected actor and params; need Boss_Fd (0x96) parent and params 100..102 or "
                     "200..217; spawned 0/1");
        return;
    }
    gZelda3dSelActor = child;
    gZelda3dActorFreeze = 1;
    sZelda3dActorPinPos = child->world.pos;
    sZelda3dActorPinRot = child->world.rot;
    Zelda3D_ReplReply(outPath, "vbball: params=%d branch=%s spawned=1/1 selected+frozen", params,
                      params >= 200 ? "death-body" : "attack-stone");
}

void ReportVolvagiaBall(const char* outPath) {
    if (gZelda3dSelActor == nullptr || gZelda3dSelActor->id != ACTOR_EN_VB_BALL) {
        Zelda3D_ReplReply(outPath, "vbinfo: scanned selected actor; need En_Vb_Ball (0xAD), inspected 0/1 typed actor");
        return;
    }
    const auto* ball = reinterpret_cast<const EnVbBall*>(gZelda3dSelActor);
    Zelda3D_ReplReply(outPath,
                      "vbinfo: inspected=1/1 params=%d shadow=%.1f size=%.3f rotVel=(%.2f,%.2f) bg=0x%x velY=%.2f "
                      "floor=%.1f",
                      ball->actor.params, ball->shadowOpacity, ball->shadowSize, ball->xRotVel, ball->yRotVel,
                      ball->actor.bgCheckFlags, ball->actor.velocity.y, ball->actor.floorHeight);
}

} // namespace

bool Zelda3D_VolvagiaBallDiagnosticsReplCommand(PlayState* play, const char* command, const char* line,
                                                const char* outPath) {
    if (std::strcmp(command, "vbball") == 0) {
        SpawnVolvagiaBall(play, line, outPath);
    } else if (std::strcmp(command, "vbinfo") == 0) {
        ReportVolvagiaBall(outPath);
    } else {
        return false;
    }
    return true;
}
