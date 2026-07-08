// See title_rider.h for the relocation rationale. Body moved verbatim from zelda3d.c's
// Zelda3D_RiderStepCue.
#include "global.h"
#include "title_rider.h"
#include "../../zelda3d_cutscene.h"

extern "C" {
// Shared math primitives (RE'd + verified vs Az; zelda3d.c) — see title_rider.h for why these
// stay there instead of moving here (still used by zelda3d.c's dead Zelda3D_RiderStep path).
int16_t Zelda3D_ActorTurnToPoint(int16_t cur_yaw, float dx, float dz, int32_t max_step);
void Zelda3D_PathFollowUpdate(float pos[3], int16_t* yaw, float* speed_xz, const int32_t waypoint[3]);
void Zelda3D_ActorMoveXZByYawSpeed(float pos[3], int16_t yaw, float speed_xz);
}

namespace Zelda3D {

void TitleRider::step(PlayState* play, int csFrame, bool* outDiscontinuity) {
    if (outDiscontinuity != nullptr) {
        *outDiscontinuity = false;
    }
    int cueIdx, startF, endF;
    int16_t cueYaw;
    float p0[3], p1[3];
    if (!Zelda3D_TitleCsRiderCue(csFrame, &cueIdx, p0, p1, &startF, &endF, &cueYaw)) {
        return; // no cue this frame — hold pose
    }
    if (cueIdx != mCueIdx) {
        // Teleport only on discontinuity: consecutive cues share p1==p0 (continued motion); a
        // shot cut authors a fresh p0.
        const float dx = p0[0] - mPos[0];
        const float dz = p0[2] - mPos[2];
        if (mCueIdx < 0 || dx * dx + dz * dz > 100.0f * 100.0f) {
            mPos[0] = p0[0];
            mPos[1] = p0[1];
            mPos[2] = p0[2];
            mYaw = cueYaw;
            if (outDiscontinuity != nullptr) {
                *outDiscontinuity = true;
            }
        }
        mCueIdx = cueIdx;
    }
    const int32_t wp[3] = { (int32_t)p1[0], (int32_t)p1[1], (int32_t)p1[2] };
    Zelda3D_PathFollowUpdate(mPos, &mYaw, &mSpeed, wp);
    Zelda3D_ActorMoveXZByYawSpeed(mPos, mYaw, mSpeed);
    // Y: Az's rider follows the terrain (Epona walks the ground); raycast SoH's floor at the
    // integrated XZ. Fall back to the cue-endpoint Y (i.e. leave mPos[1] as PathFollowUpdate/
    // ActorMoveXZByYawSpeed left it) when there's no floor.
    {
        Vec3f q = { mPos[0], mPos[1] + 200.0f, mPos[2] };
        CollisionPoly poly;
        f32 y = BgCheck_AnyRaycastFloor1(&play->colCtx, &poly, &q);
        if (y > BGCHECK_Y_MIN + 1.0f) {
            mPos[1] = y;
        }
    }
}

} // namespace Zelda3D
