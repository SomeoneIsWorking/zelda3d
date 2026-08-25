#include "oracle_skeleton_compare.h"

#include <cstdio>

#include "soh_animation_state.h"
#include "soh_play_state.h"

namespace HarnessOracle {

void CompareSkeletonImpl(int category, int listIndex) {
    std::printf("  3ds: n/a (Actor SkelAnime offset in OoT3D not RE'd yet)\n");
    if (!SohState_HasPlayState()) {
        std::printf("  soh: n/a (no playstate)\n");
        return;
    }
    short joints[32 * 3] = {};
    int jointCount = 0;
    int animationFrame = 0;
    int morphFrame = 0;
    const int written =
        SohState_ActorSkeleton(category, listIndex, joints, 32, &jointCount, &animationFrame, &morphFrame);
    if (written < 0) {
        std::printf("  soh: n/a (actor at cat=%d idx=%d not present)\n", category, listIndex);
        return;
    }
    if (written == 0) {
        std::printf("  soh: cat=%d idx=%d has no SkelAnime\n", category, listIndex);
        return;
    }
    std::printf("  soh: cat=%d idx=%d limbs=%d animFrame=%d morphWeightBits=0x%08x\n", category, listIndex, jointCount,
                animationFrame, static_cast<unsigned>(morphFrame));
    for (int index = 0; index < written; ++index) {
        std::printf("       joint[%d] = (%d, %d, %d)\n", index, joints[index * 3], joints[index * 3 + 1],
                    joints[index * 3 + 2]);
    }
}

} // namespace HarnessOracle
