#include "actor_camera.h"

#include "../../anim/pose_tracking.h"
#include "../../diagnostics/zelda3d_diagnostics.h"
#include "../../render/actor_draw_observation.h"
#include "../../render/actor_model_submission.h"
#include "../repl_camera_state.h"
#include "../zelda3d_repl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

bool Zelda3D_ActorCameraReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    if (strcmp(command, "aaim") == 0 || strcmp(command, "aorbit") == 0) {
        // GENERIC draw-position-aware framing: aim at where the selected actor's OoT3D MODEL actually
        // draws — its posed world-space center — not its world.pos anchor. Essential for posed/offset
        // actors (Queen Gohma hangs on the ceiling far above her floor anchor, flying creatures, held
        // items) where `acam` (anchor-based) points at empty space.
        //   `aaim [dist] [axis]`   — side profile like acam; dist default = auto (3x model radius).
        //   `aorbit <dist> <yaw> <pitch>` — orbit the same center at spherical (deg) angles.
        // Needs the selection to have DRAWN once (Zelda3D_EmitModelDraw records its model + transform and
        // enables posed-skin caching); call after asel and a frame or two of running.
        int isOrbit = (command[1] == 'o');
        if (gZelda3dSelActor == NULL || sZelda3dSelDrawModel < 0) {
            Zelda3D_ReplReply(outPath, "%s: no DRAWN selection (asel + let the actor draw a frame)", command);
        } else {
            float mn[3], mx[3];
            if (!Zelda3D_PosedModelLocalAABB(sZelda3dSelDrawModel, ~0ull, mn, mx)) {
                Zelda3D_ReplReply(outPath, "%s: no posed AABB yet (let a frame pass after asel)", command);
            } else {
                Actor* a = gZelda3dSelActor;
                float s = sZelda3dSelDrawScale;
                // model-local center; the generic ground offset is applied innermost (pre-scale) ONLY
                // when there is no faithful draw-space transform (which REPLACES it — see EmitModelDraw).
                float go = sZelda3dSelDrawDsHave ? 0.0f : sZelda3dSelDrawGroundOff;
                float lx = (mn[0] + mx[0]) * 0.5f, ly = (mn[1] + mx[1]) * 0.5f + go, lz = (mn[2] + mx[2]) * 0.5f;
                // Faithful draw-space LOCAL translate (e.g. Gohma's -4000) is applied after shape.rot
                // but before worldScale, i.e. added in the rotated, world-unit frame: fold it in here
                // (pre-rotate) alongside scale*localCenter so the rotation below carries both.
                float vx = lx * s + sZelda3dSelDrawDsLocal[0], vy = ly * s + sZelda3dSelDrawDsLocal[1],
                      vz = lz * s + sZelda3dSelDrawDsLocal[2];
                // rotate by the actor's YXZ shape.rot (EmitModelDraw order: Ry*Rx*Rz applied to scale*L).
                const float B2R = 3.14159265358979f / 32768.0f;
                float rx = a->shape.rot.x * B2R, ry = a->shape.rot.y * B2R, rz = a->shape.rot.z * B2R;
                float cz_ = cosf(rz), sz_ = sinf(rz); // Rz
                float x1 = cz_ * vx - sz_ * vy, y1 = sz_ * vx + cz_ * vy, z1 = vz;
                float cx_ = cosf(rx), sx_ = sinf(rx); // Rx
                float x2 = x1, y2 = cx_ * y1 - sx_ * z1, z2 = sx_ * y1 + cx_ * z1;
                float cy_ = cosf(ry), sy_ = sinf(ry); // Ry
                float x3 = cy_ * x2 + sy_ * z2, y3 = y2, z3 = -sy_ * x2 + cy_ * z2;
                // world.pos + (0, dsLiftY, 0) [world frame] + Rot*(scale*localCenter + dsLocal).
                gZelda3dAimCenter[0] = a->world.pos.x + x3;
                gZelda3dAimCenter[1] = a->world.pos.y + sZelda3dSelDrawDsLiftY + y3;
                gZelda3dAimCenter[2] = a->world.pos.z + z3;
                float dx = (mx[0] - mn[0]) * s, dy = (mx[1] - mn[1]) * s, dz = (mx[2] - mn[2]) * s;
                gZelda3dAimRadius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
                if (gZelda3dAimRadius < 1.0f)
                    gZelda3dAimRadius = 1.0f;
                float cx = gZelda3dAimCenter[0], cy = gZelda3dAimCenter[1], cz = gZelda3dAimCenter[2];
                gZelda3dCamAt[0] = cx;
                gZelda3dCamAt[1] = cy;
                gZelda3dCamAt[2] = cz;
                if (isOrbit) {
                    float dist = 0.0f, yawD = 0.0f, pitchD = 15.0f;
                    (void)sscanf(line, "%*s %f %f %f", &dist, &yawD, &pitchD);
                    if (dist <= 0.0f)
                        dist = gZelda3dAimRadius * 3.0f;
                    float yaw = yawD * (3.14159265f / 180.0f), pit = pitchD * (3.14159265f / 180.0f);
                    gZelda3dCamEye[0] = cx + dist * cosf(pit) * sinf(yaw);
                    gZelda3dCamEye[1] = cy + dist * sinf(pit);
                    gZelda3dCamEye[2] = cz + dist * cosf(pit) * cosf(yaw);
                    gZelda3dCamOverride = 1;
                    Zelda3D_ReplReply(outPath, "aorbit center=(%.0f,%.0f,%.0f) r=%.0f dist=%.0f yaw=%.0f pitch=%.0f",
                                      cx, cy, cz, gZelda3dAimRadius, dist, yawD, pitchD);
                } else {
                    float dist = 0.0f;
                    int axis = 0;
                    (void)sscanf(line, "%*s %f %d", &dist, &axis);
                    if (dist <= 0.0f)
                        dist = gZelda3dAimRadius * 3.0f;
                    gZelda3dCamEye[0] = cx + (axis == 0 ? dist : 0.0f);
                    gZelda3dCamEye[1] = cy + gZelda3dAimRadius * 0.4f;
                    gZelda3dCamEye[2] = cz + (axis == 0 ? 0.0f : dist);
                    gZelda3dCamOverride = 1;
                    Zelda3D_ReplReply(outPath, "aaim center=(%.0f,%.0f,%.0f) r=%.0f dist=%.0f axis=%d (model %d)", cx,
                                      cy, cz, gZelda3dAimRadius, dist, axis, sZelda3dSelDrawModel);
                }
            }
        }
    } else {
        return false;
    }
    return true;
}
