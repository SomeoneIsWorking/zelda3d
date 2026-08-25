#pragma once

namespace HarnessOracle {

enum class DivClass : int {
    Unclassified = 0,
    PermanentNoise = 1,
    DeferredPortTarget = 2,
};

struct DivDecision {
    DivClass cls = DivClass::Unclassified;
    const char* tag = "";
    const char* origin_az = "";
    const char* origin_soh = "";
    const char* origin_doc = "";
};

extern const DivDecision kUnclassified;

const char* DivClassStr(DivClass classification);
DivDecision ClassifyD6Content(int categoryDeltaIndex, int oracleCount, int sohCount, int oracleWonderTalk2Extra);
DivDecision ClassifyD7Worst(int worstCategory, int worstId);
DivDecision ClassifyD3PlayerPos(float sohSpeedXz, float oracleSpeedXz, unsigned int sohBgFlags,
                                unsigned int oracleBgFlags);
short RadToBinaryAngle(float radians);

} // namespace HarnessOracle
