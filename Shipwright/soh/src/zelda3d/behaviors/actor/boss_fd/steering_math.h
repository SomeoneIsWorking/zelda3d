// OoT3D-authored steering math used by Boss_Fd's 30 Hz flight producer.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_STEERING_MATH_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_STEERING_MATH_H

#include <cstdint>

namespace Zelda3D::BossFdSteeringMath {

std::int16_t WrapBinAngle(float value);
float SinS(std::int16_t angle);
float CosS(std::int16_t angle);
float Atan2(float y, float x);

} // namespace Zelda3D::BossFdSteeringMath

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_STEERING_MATH_H
