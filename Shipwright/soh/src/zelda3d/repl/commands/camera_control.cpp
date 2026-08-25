#include "camera_control.h"

#include "../../render/camera_reconcile.h"
#include "../../render/model_group_diagnostics.h"
#include "../../render/room_render.h"
#include "../../scene/cinematic_camera_state.h"
#include "../../scene/scene_transform.h"
#include "../repl_camera_state.h"
#include "../zelda3d_repl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

bool Zelda3D_CameraControlReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    float f1;
    float f2;
    float f3;
    int iv;
    if (strcmp(command, "sceneoff") == 0 && sscanf(line, "%*s %f %f %f", &f1, &f2, &f3) == 3) {
        gZelda3dSceneOffX = f1;
        gZelda3dSceneOffY = f2;
        gZelda3dSceneOffZ = f3;
        Zelda3D_ReplReply(outPath, "sceneoff=(%.1f,%.1f,%.1f)", gZelda3dSceneOffX, gZelda3dSceneOffY,
                          gZelda3dSceneOffZ);
    } else if (strcmp(command, "camfreeze") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Capture the current camera and hold it (1), or release back to the engine (0).
        if (f1 != 0.0f) {
            gZelda3dCamEye[0] = play->view.eye.x;
            gZelda3dCamEye[1] = play->view.eye.y;
            gZelda3dCamEye[2] = play->view.eye.z;
            gZelda3dCamAt[0] = play->view.lookAt.x;
            gZelda3dCamAt[1] = play->view.lookAt.y;
            gZelda3dCamAt[2] = play->view.lookAt.z;
            gZelda3dCamOverride = 1;
            Zelda3D_ReplReply(outPath, "camfreeze ON eye=(%.0f,%.0f,%.0f) at=(%.0f,%.0f,%.0f)", gZelda3dCamEye[0],
                              gZelda3dCamEye[1], gZelda3dCamEye[2], gZelda3dCamAt[0], gZelda3dCamAt[1],
                              gZelda3dCamAt[2]);
        } else {
            gZelda3dCamOverride = 0;
            Zelda3D_ReplReply(outPath, "camfreeze OFF (camera returned to engine)");
        }
    } else if (strcmp(command, "cam") == 0) {
        // cam <eyeX eyeY eyeZ atX atY atZ> — set the frozen camera explicitly + hold it.
        float c[6];
        if (sscanf(line, "%*s %f %f %f %f %f %f", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5]) == 6) {
            gZelda3dCamEye[0] = c[0];
            gZelda3dCamEye[1] = c[1];
            gZelda3dCamEye[2] = c[2];
            gZelda3dCamAt[0] = c[3];
            gZelda3dCamAt[1] = c[4];
            gZelda3dCamAt[2] = c[5];
            gZelda3dCamOverride = 1;
            Zelda3D_ReplReply(outPath, "cam eye=(%.0f,%.0f,%.0f) at=(%.0f,%.0f,%.0f)", c[0], c[1], c[2], c[3], c[4],
                              c[5]);
        } else {
            Zelda3D_ReplReply(outPath, "cam needs 6 floats: eyeX eyeY eyeZ atX atY atZ");
        }
    } else if (strcmp(command, "camlift") == 0) {
        // #4 toggle/inspect the cutscene/title camera-lift. `camlift 0|1` sets it; `camlift` alone
        // reports state + the live view eye and the lift applied THIS frame (post-reconcile).
        if (sscanf(line, "%*s %i", &iv) == 1) {
            gZelda3dCamLift = iv ? 1 : 0;
        }
        Zelda3D_ReplReply(outPath, "camlift=%d csState=%d activeCam=%d view.eye=(%.0f,%.0f,%.0f) lift=%.1f",
                          gZelda3dCamLift, play->csCtx.state, play->activeCamera, play->view.eye.x, play->view.eye.y,
                          play->view.eye.z, gZelda3dCamLiftLast);
    } else if (strcmp(command, "camdraw") == 0) {
        // `camdraw <modelId> <groupIdx> [dist]` — point the frozen camera at a specific draw
        // GROUP, framed from its own vertex AABB. `acam` frames ACTORS, so room geometry had no
        // way to be brought on screen: verifying the alpha-test port stalled because the Castle
        // Courtyard window group (model 1001 g16) is submitted every frame but sits off-camera at
        // the spawn point, giving a 0-pixel isolated footprint with nothing to measure.
        // Scene room CMBs store WORLD-space vertices under an identity model matrix, so the group
        // AABB is the world box directly; for ACTOR models it is model-local and this will aim at
        // the wrong place -- use `acam` for those.
        int mid = 0, gidx = 0;
        float dist = 0.0f;
        int n = sscanf(line, "%*s %i %i %f", &mid, &gidx, &dist);
        if (n < 2) {
            Zelda3D_ReplReply(outPath, "usage: camdraw <modelId> <groupIdx> [dist]");
        } else {
            float mn[3], mx[3];
            if (!Zelda3D_Sg_GroupBounds(mid, gidx, mn, mx)) {
                Zelda3D_ReplReply(outPath,
                                  "camdraw: model %d group %d has no bounds (not uploaded, "
                                  "or the group has no vertices)",
                                  mid, gidx);
            } else {
                const float cx = 0.5f * (mn[0] + mx[0]);
                const float cy = 0.5f * (mn[1] + mx[1]);
                const float cz = 0.5f * (mn[2] + mx[2]);
                float ext = mx[0] - mn[0];
                if (mx[1] - mn[1] > ext)
                    ext = mx[1] - mn[1];
                if (mx[2] - mn[2] > ext)
                    ext = mx[2] - mn[2];
                if (dist <= 0.0f)
                    dist = (ext > 1.0f ? ext : 1.0f) * 1.8f;
                gZelda3dCamEye[0] = cx + dist;
                gZelda3dCamEye[1] = cy + dist * 0.35f;
                gZelda3dCamEye[2] = cz + dist;
                gZelda3dCamAt[0] = cx;
                gZelda3dCamAt[1] = cy;
                gZelda3dCamAt[2] = cz;
                gZelda3dCamOverride = 1;
                Zelda3D_ReplReply(outPath,
                                  "camdraw model=%d g%d bounds=(%.0f,%.0f,%.0f)..(%.0f,%.0f,%.0f) "
                                  "centre=(%.0f,%.0f,%.0f) dist=%.0f",
                                  mid, gidx, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2], cx, cy, cz, dist);
            }
        }
    } else if (strcmp(command, "camorbit") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Rotate the frozen eye about the frozen `at` by f1 degrees around world +Y,
        // preserving radius and height. Auto-freezes from the live camera first if not
        // already held, so `camorbit 15` works without a prior `camfreeze 1`. This is
        // the parallax-sweep primitive: hold `at`, step the azimuth, dump at each step.
        float dx, dz, c, s, nx, nz, rad;
        if (!gZelda3dCamOverride) {
            gZelda3dCamEye[0] = play->view.eye.x;
            gZelda3dCamEye[1] = play->view.eye.y;
            gZelda3dCamEye[2] = play->view.eye.z;
            gZelda3dCamAt[0] = play->view.lookAt.x;
            gZelda3dCamAt[1] = play->view.lookAt.y;
            gZelda3dCamAt[2] = play->view.lookAt.z;
            gZelda3dCamOverride = 1;
        }
        dx = gZelda3dCamEye[0] - gZelda3dCamAt[0];
        dz = gZelda3dCamEye[2] - gZelda3dCamAt[2];
        c = cosf(f1 * (3.14159265f / 180.0f));
        s = sinf(f1 * (3.14159265f / 180.0f));
        nx = dx * c - dz * s;
        nz = dx * s + dz * c;
        gZelda3dCamEye[0] = gZelda3dCamAt[0] + nx;
        gZelda3dCamEye[2] = gZelda3dCamAt[2] + nz;
        rad = sqrtf(nx * nx + nz * nz);
        Zelda3D_ReplReply(outPath, "camorbit %+.1fdeg eye=(%.0f,%.0f,%.0f) at=(%.0f,%.0f,%.0f) rad=%.0f", f1,
                          gZelda3dCamEye[0], gZelda3dCamEye[1], gZelda3dCamEye[2], gZelda3dCamAt[0], gZelda3dCamAt[1],
                          gZelda3dCamAt[2], rad);
    } else {
        return false;
    }
    return true;
}
