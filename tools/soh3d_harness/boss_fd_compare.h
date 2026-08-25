#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_COMPARE_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_COMPARE_H

#include <cstdint>

enum class BossFdCompareStatus {
    Match,
    Diverged,
    Missing,
    Invalid,
};

BossFdCompareStatus CompareBossFd(uint32_t azPlayState);
BossFdCompareStatus LastBossFdCompareStatus();
const char* BossFdCompareStatusName(BossFdCompareStatus status);

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_COMPARE_H
