#include "actor_transform_control.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "../../diagnostics/actor_selection.h"
#include "../../render/actor_control_state.h"
#include "../repl_camera_state.h"
#include "../zelda3d_repl.h"

namespace {

void SetFreeze(const char* line, const char* outPath) {
    int mode = 0;
    if (std::sscanf(line, "%*s %i", &mode) == 1) {
        gZelda3dActorFreeze = mode < 0 || mode > 2 ? (mode ? 1 : 0) : mode;
        if (gZelda3dActorFreeze && gZelda3dSelActor != nullptr) {
            sZelda3dActorPinPos = gZelda3dSelActor->world.pos;
            sZelda3dActorPinRot = gZelda3dSelActor->world.rot;
        }
    }
    Zelda3D_ReplReply(outPath, "afreeze=%d (1=pos+rot,2=pos only) sel=%s", gZelda3dActorFreeze,
                      gZelda3dSelActor ? "set" : "NONE (asel first)");
}

void SetPosition(const char* line, const char* outPath) {
    float position[3] = {};
    if (gZelda3dSelActor == nullptr) {
        Zelda3D_ReplReply(outPath, "apos: no selection (asel first)");
    } else if (std::sscanf(line, "%*s %f %f %f", &position[0], &position[1], &position[2]) != 3) {
        Zelda3D_ReplReply(outPath, "apos needs x y z");
    } else {
        gZelda3dSelActor->world.pos.x = sZelda3dActorPinPos.x = position[0];
        gZelda3dSelActor->world.pos.y = sZelda3dActorPinPos.y = position[1];
        gZelda3dSelActor->world.pos.z = sZelda3dActorPinPos.z = position[2];
        Zelda3D_ReplReply(outPath, "apos=(%.0f,%.0f,%.0f)", position[0], position[1], position[2]);
    }
}

void SetRotation(const char* line, const char* outPath) {
    int rotation[3] = {};
    if (gZelda3dSelActor == nullptr) {
        Zelda3D_ReplReply(outPath, "arot: no selection (asel first)");
    } else if (std::sscanf(line, "%*s %d %d %d", &rotation[0], &rotation[1], &rotation[2]) != 3) {
        Zelda3D_ReplReply(outPath, "arot needs x y z (binang)");
    } else {
        sZelda3dActorPinRot = { static_cast<s16>(rotation[0]), static_cast<s16>(rotation[1]),
                                static_cast<s16>(rotation[2]) };
        gZelda3dSelActor->world.rot = gZelda3dSelActor->shape.rot = sZelda3dActorPinRot;
        Zelda3D_ReplReply(outPath, "arot=(%d,%d,%d)", rotation[0], rotation[1], rotation[2]);
    }
}

void SetParams(const char* line, const char* outPath) {
    int params = 0;
    if (gZelda3dSelActor == nullptr) {
        Zelda3D_ReplReply(outPath, "aparams: no selection (asel first)");
    } else if (std::sscanf(line, "%*s %i", &params) == 1) {
        gZelda3dSelActor->params = static_cast<s16>(params);
        Zelda3D_ReplReply(outPath, "aparams=%d", gZelda3dSelActor->params);
    } else {
        Zelda3D_ReplReply(outPath, "aparams=%d", gZelda3dSelActor->params);
    }
}

void FrameActor(const char* line, const char* outPath) {
    float distance = 110.0f;
    float elevation = 0.0f;
    int axis = 0;
    (void)std::sscanf(line, "%*s %f %d %f", &distance, &axis, &elevation);
    elevation = std::fmax(-89.0f, std::fmin(89.0f, elevation));
    if (gZelda3dSelActor == nullptr) {
        Zelda3D_ReplReply(outPath, "acam: no selection (asel first)");
        return;
    }

    const float centerX = gZelda3dSelActor->world.pos.x;
    const float centerY = gZelda3dSelActor->world.pos.y + 12.0f;
    const float centerZ = gZelda3dSelActor->world.pos.z;
    gZelda3dCamAt[0] = centerX;
    gZelda3dCamAt[1] = centerY;
    gZelda3dCamAt[2] = centerZ;
    const float radians = elevation * 3.14159265f / 180.0f;
    const float horizontal = distance * std::cos(radians);
    const float vertical = distance * std::sin(radians);
    gZelda3dCamEye[0] = centerX + (axis == 0 ? horizontal : 0.0f);
    gZelda3dCamEye[1] = centerY + 14.0f + vertical;
    gZelda3dCamEye[2] = centerZ + (axis == 0 ? 0.0f : horizontal);
    gZelda3dCamOverride = 1;
    Zelda3D_ReplReply(outPath, "acam at=(%.0f,%.0f,%.0f) dist=%.0f axis=%d elev=%.0f eye=(%.0f,%.0f,%.0f)", centerX,
                      centerY, centerZ, distance, axis, elevation, gZelda3dCamEye[0], gZelda3dCamEye[1],
                      gZelda3dCamEye[2]);
}

} // namespace

bool Zelda3D_ActorTransformControlReplCommand(PlayState* play, const char* command, const char* line,
                                              const char* outPath) {
    (void)play;
    if (std::strcmp(command, "afreeze") == 0) {
        SetFreeze(line, outPath);
    } else if (std::strcmp(command, "apos") == 0) {
        SetPosition(line, outPath);
    } else if (std::strcmp(command, "arot") == 0) {
        SetRotation(line, outPath);
    } else if (std::strcmp(command, "aparams") == 0) {
        SetParams(line, outPath);
    } else if (std::strcmp(command, "acam") == 0) {
        FrameActor(line, outPath);
    } else {
        return false;
    }
    return true;
}
