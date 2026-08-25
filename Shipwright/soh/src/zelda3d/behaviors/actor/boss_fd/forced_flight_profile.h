// Shared constant-control profile for paired Boss_Fd producer verification.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_FORCED_FLIGHT_PROFILE_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_FORCED_FLIGHT_PROFILE_H

namespace Zelda3D::BossFdForcedProfile {

inline constexpr int kAction = 0;
inline constexpr int kMoveTimer = 0;
inline constexpr int kActionTimer = 30000;
inline constexpr float kTargetX = 0.0f;
inline constexpr float kTargetY = 500.0f;
inline constexpr float kTargetZ = 300.0f;
// FUN_003C724C rewrites the fly-speed control (+0x90C) from its literal pool every tick while
// substate +0x229e == 0: `*(+0x90C) = *(0x003c76d0)`. The pool word is 0x40555555 = 10/3, so
// kSpeed must be that constant — any other forced value is overwritten by the genuine oracle
// within one tick and the paired comparison can never hold. (Measured live: pool read back
// 0x40555555 via the harness; oracle control slot settled at exactly 3.333.) The authored
// producer (authored_flight.cpp kFlySpeedControl) performs the same writeback on our side.
inline constexpr float kSpeed = 10.0f / 3.0f;
inline constexpr float kTurnRate = 1000.0f;
inline constexpr float kTurnRateMax = 1000.0f;
inline constexpr float kWobbleAmplitude = 20.0f;
inline constexpr float kWobbleRate = 0.0f;

} // namespace Zelda3D::BossFdForcedProfile

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_FORCED_FLIGHT_PROFILE_H
