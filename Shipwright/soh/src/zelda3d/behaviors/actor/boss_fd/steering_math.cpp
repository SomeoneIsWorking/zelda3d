// OoT3D-authored steering math used by Boss_Fd's 30 Hz flight producer.
#include "steering_math.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Zelda3D::BossFdSteeringMath {
namespace {

#include "oot3d_trig_table.inc"

float Interpolate(std::uint16_t phase, std::size_t valueIndex, std::size_t deltaIndex) {
    const auto& entry = kTrigTableBits[phase >> 8];
    const float value = std::bit_cast<float>(entry[valueIndex]);
    const float delta = std::bit_cast<float>(entry[deltaIndex]);
    const float fraction = static_cast<float>(phase & 0xFFU) * (1.0F / 256.0F);
    volatile float scaledDelta = delta * fraction;
    return value + scaledDelta;
}

constexpr float FromBits(std::uint32_t bits) {
    return std::bit_cast<float>(bits);
}

float Add(float left, float right) {
    volatile float result = left + right;
    return result;
}

float Subtract(float left, float right) {
    volatile float result = left - right;
    return result;
}

float Multiply(float left, float right) {
    volatile float result = left * right;
    return result;
}

float Divide(float numerator, float denominator) {
    volatile float result = numerator / denominator;
    return result;
}

float CopySign(float magnitude, float sign) {
    return std::bit_cast<float>((std::bit_cast<std::uint32_t>(magnitude) & 0x7FFFFFFFU) |
                                (std::bit_cast<std::uint32_t>(sign) & 0x80000000U));
}

float EvaluateAtanPolynomial(float value, float baseHigh, float baseLow) {
    constexpr float kCoefficient1 = FromBits(0xBEAAAAA8U);
    constexpr float kCoefficient2 = FromBits(0x3E4CC861U);
    constexpr float kCoefficient3 = FromBits(0xBE11B50FU);
    constexpr float kCoefficient4 = FromBits(0x3DD5B88FU);
    constexpr float kCoefficient5 = FromBits(0xBD65AD2DU);

    const float square = Multiply(value, value);
    float polynomial = Add(kCoefficient4, Multiply(square, kCoefficient5));
    polynomial = Add(kCoefficient3, Multiply(square, polynomial));
    polynomial = Add(kCoefficient2, Multiply(square, polynomial));
    polynomial = Add(kCoefficient1, Multiply(square, polynomial));
    const float correction = Add(baseLow, Multiply(Multiply(value, square), polynomial));
    return Add(Add(correction, value), baseHigh);
}

} // namespace

std::int16_t WrapBinAngle(float value) {
    const auto integer = static_cast<std::int32_t>(value);
    const auto lowBits = static_cast<std::uint16_t>(integer);
    return std::bit_cast<std::int16_t>(lowBits);
}

float SinS(std::int16_t angle) {
    return Interpolate(std::bit_cast<std::uint16_t>(angle), 0, 2);
}

float CosS(std::int16_t angle) {
    return Interpolate(std::bit_cast<std::uint16_t>(angle), 1, 3);
}

float Atan2(float y, float x) {
    constexpr float kPiOver2 = FromBits(0x3FC90FDBU);
    constexpr float kPi = FromBits(0x40490FDBU);
    constexpr float kPiOver2High = FromBits(0x3FC90000U);
    constexpr float kPiOver2Low = FromBits(0x39FDAA22U);
    constexpr float kPiHigh = FromBits(0x40490000U);
    constexpr float kPiLow = FromBits(0x3A7DAA22U);
    constexpr float kAtanHalfHigh = FromBits(0x3EED6000U);
    constexpr float kAtanHalfLow = FromBits(0x37CE0AC3U);

    const std::uint32_t yBits = std::bit_cast<std::uint32_t>(y);
    const std::uint32_t xBits = std::bit_cast<std::uint32_t>(x);
    const std::uint32_t yMagnitude = yBits & 0x7FFFFFFFU;
    const std::uint32_t xMagnitude = xBits & 0x7FFFFFFFU;

    if (yMagnitude > 0x7F800000U || xMagnitude > 0x7F800000U) {
        return Add(y, x);
    }
    if (yMagnitude == 0U) {
        return (xBits & 0x80000000U) != 0U ? CopySign(kPi, y) : y;
    }
    if (xMagnitude == 0U) {
        return CopySign(kPiOver2, y);
    }
    if (yMagnitude == 0x7F800000U && xMagnitude == 0x7F800000U) {
        const float diagonal = FromBits(0x3F490FDBU);
        return (xBits & 0x80000000U) != 0U ? CopySign(Multiply(3.0F, diagonal), y) : CopySign(diagonal, y);
    }
    if (yMagnitude == 0x7F800000U) {
        return CopySign(kPiOver2, y);
    }
    if (xMagnitude == 0x7F800000U) {
        return (xBits & 0x80000000U) != 0U ? CopySign(kPi, y) : CopySign(0.0F, y);
    }

    const int exponentDistance =
        static_cast<int>((yMagnitude << 1U) >> 24U) - static_cast<int>((xMagnitude << 1U) >> 24U);
    if (exponentDistance > 27) {
        return CopySign(kPiOver2, y);
    }
    if (exponentDistance < -26) {
        return (xBits & 0x80000000U) != 0U ? CopySign(kPi, y) : Divide(y, x);
    }

    float numerator;
    float denominator;
    float baseHigh;
    float baseLow;
    if (yMagnitude > xMagnitude) {
        numerator = x;
        denominator = -y;
        baseHigh = CopySign(kPiOver2High, y);
        baseLow = CopySign(kPiOver2Low, y);
    } else {
        numerator = y;
        denominator = x;
        if ((xBits & 0x80000000U) == 0U) {
            baseHigh = 0.0F;
            baseLow = 0.0F;
        } else {
            baseHigh = CopySign(kPiHigh, y);
            baseLow = CopySign(kPiLow, y);
        }
    }

    const std::uint32_t numeratorBits = std::bit_cast<std::uint32_t>(numerator);
    const std::uint32_t denominatorBits = std::bit_cast<std::uint32_t>(denominator);
    float reduced;
    if ((denominatorBits - numeratorBits) * 2U < 0x01000000U) {
        const bool sameSign = ((denominatorBits ^ numeratorBits) & 0x80000000U) == 0U;
        const float half = sameSign ? 0.5F : -0.5F;
        baseHigh = sameSign ? Add(baseHigh, kAtanHalfHigh) : Subtract(baseHigh, kAtanHalfHigh);
        baseLow = sameSign ? Add(baseLow, kAtanHalfLow) : Subtract(baseLow, kAtanHalfLow);
        reduced = Divide(Subtract(numerator, Multiply(half, denominator)), Add(denominator, Multiply(numerator, half)));
    } else {
        reduced = Divide(numerator, denominator);
    }
    return EvaluateAtanPolynomial(reduced, baseHigh, baseLow);
}

} // namespace Zelda3D::BossFdSteeringMath
