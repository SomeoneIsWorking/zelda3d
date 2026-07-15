// Zelda3D math/locomotion primitives ported from OoT3D's z_actor.c (Actor_TurnToPoint,
// PathFollow_Update, Actor_MoveXZByYawSpeed) — see oot3d-decomp docs/title_writer_chains.md.
// Extracted out of zelda3d.c (Phase 2b codebase reorg step 3, see docs/codemap.md) into
// zelda3d/core/zelda3d_math.cpp. Called by behaviors/title/title_rider.cpp's TitleRider::step()
// via its own local forward declarations (unchanged by this move) — this header is for any
// future consumer that wants them without hunting through zelda3d.c/title_rider.cpp.
#ifndef ZELDA3D_MATH_H
#define ZELDA3D_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int16_t Zelda3D_ActorTurnToPoint(int16_t cur_yaw, float dx, float dz, int32_t max_step);
void Zelda3D_PathFollowUpdate(float pos[3], int16_t* yaw, float* speed_xz, const int32_t waypoint[3]);
void Zelda3D_ActorMoveXZByYawSpeed(float pos[3], int16_t yaw, float speed_xz);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_MATH_H
