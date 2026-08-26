#include "Shipwright/soh/src/zelda3d/behaviors/actor/boss_fd/steering_math.h"

#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>

using Zelda3D::BossFdSteeringMath::Atan2;
using Zelda3D::BossFdSteeringMath::CosS;
using Zelda3D::BossFdSteeringMath::SinS;
using Zelda3D::BossFdSteeringMath::WrapBinAngle;

int main() {
    assert(WrapBinAngle(0.9F) == 0);
    assert(WrapBinAngle(-1.9F) == -1);
    assert(WrapBinAngle(65537.0F) == 1);

    assert(std::bit_cast<std::uint32_t>(SinS(0)) == 0U);
    assert(std::bit_cast<std::uint32_t>(SinS(0x0100)) == 0x3CC90AB0U);
    assert(std::bit_cast<std::uint32_t>(SinS(0x4000)) == 0x3F800000U);
    assert(std::bit_cast<std::uint32_t>(CosS(0)) == 0x3F800000U);
    assert(std::bit_cast<std::uint32_t>(CosS(0x4000)) == 0U);

    assert(SinS(1) > 0.0F);
    assert(SinS(1) < SinS(0x0100));
    assert(std::abs(SinS(1) + SinS(-1)) < 1.0e-6F);
    assert(CosS(1) < 1.0F);
    assert(CosS(1) > CosS(0x0100));

    assert(std::bit_cast<std::uint32_t>(Atan2(0.0F, 1.0F)) == 0U);
    assert(std::bit_cast<std::uint32_t>(Atan2(1.0F, 0.0F)) == 0x3FC90FDBU);
    assert(std::bit_cast<std::uint32_t>(Atan2(1.0F, 1.0F)) == 0x3F490FDBU);
    assert(std::bit_cast<std::uint32_t>(Atan2(-1.0F, -1.0F)) == 0xC016CBE4U);
}
