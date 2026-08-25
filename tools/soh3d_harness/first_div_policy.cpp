#include "first_div_policy.h"

#include <cmath>

namespace HarnessOracle {

const DivDecision kUnclassified{ DivClass::Unclassified, "", "", "", "" };

const char* DivClassStr(DivClass classification) {
    switch (classification) {
        case DivClass::PermanentNoise:
            return "NOISE";
        case DivClass::DeferredPortTarget:
            return "DEFERRED";
        default:
            return "";
    }
}

DivDecision ClassifyD6Content(int categoryDeltaIndex, int oracleCount, int sohCount, int oracleWonderTalk2Extra) {
    if (oracleWonderTalk2Extra == 1 && oracleCount - sohCount == 1 && categoryDeltaIndex == 1) {
        return { DivClass::PermanentNoise, "wonder_talk2", "cat=1 id=0x0185 (En_Wonder_Talk2)",
                 "no SoH equivalent — 3DS-only", "oot3d-decomp/docs/gameplay_firstdiv.md#sign-blind-policy" };
    }
    return kUnclassified;
}

DivDecision ClassifyD7Worst(int worstCategory, int worstId) {
    if (worstCategory == 7 && worstId == 0x0018) {
        return { DivClass::PermanentNoise, "navi-rng", "cat=7 id=0x0018 (En_Elf/Navi)",
                 "same (En_Elf) — RNG seed divergence", "oot3d-decomp/docs/gameplay_firstdiv.md#rng-determinism" };
    }
    return kUnclassified;
}

DivDecision ClassifyD3PlayerPos(float sohSpeedXz, float oracleSpeedXz, unsigned int sohBgFlags,
                                unsigned int oracleBgFlags) {
    constexpr unsigned int kWallFlag = 0x0008u;
    if ((sohBgFlags & kWallFlag) != 0 || (oracleBgFlags & kWallFlag) != 0) {
        return { DivClass::DeferredPortTarget, "collision-wall", "Player Actor+0x0090 bgCheckFlags & 0x008 (soh3d TBD)",
                 "Actor.bgCheckFlags & 0x008 (z64actor.h:237, :281)",
                 "oot3d-decomp/docs/gameplay_firstdiv.md#scene-collision" };
    }
    if (std::fabs(sohSpeedXz - oracleSpeedXz) < 2.0f && sohSpeedXz > 0.1f && oracleSpeedXz > 0.1f) {
        return { DivClass::PermanentNoise, "rate-comp", "Player Actor+0x068 speedXZ (soh3d f70e927)",
                 "Actor.speedXZ (z64actor.h:227)", "oot3d-decomp/docs/gameplay_firstdiv.md#per-frame-firstdiv" };
    }
    return kUnclassified;
}

short RadToBinaryAngle(float radians) {
    constexpr float kBinaryAngleScale = 10430.378350470453f;
    const auto value = static_cast<long long>(radians * kBinaryAngleScale);
    return static_cast<short>(value & 0xFFFF);
}

} // namespace HarnessOracle
