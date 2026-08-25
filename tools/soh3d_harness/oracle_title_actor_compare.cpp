#include "oracle_title_actor_compare.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "oracle_state.h"
#include "oracle_title_state.h"
#include "soh_animation_state.h"
#include "soh_play_state.h"

namespace HarnessOracle {

void CompareTitleActorsImpl() {
    if (!TitleActive()) {
        std::printf("  3ds: n/a (not at title)\n");
    } else {
        auto& memory = Core::System::GetInstance().Memory();
        std::printf("  3ds: 25 poses @ 0x%08x  {Vec3 pos, Vec3 rot(rad), Vec3 scale}\n",
                    OracleLayout::kTitlePoseTableAddress);
        for (uint32_t index = 0; index < OracleLayout::kTitlePoseCount; ++index) {
            const uint32_t address = OracleLayout::kTitlePoseTableAddress + index * OracleLayout::kTitlePoseStride;
            float position[3] = {};
            float rotation[3] = {};
            bool mapped = true;
            for (int axis = 0; axis < 3; ++axis) {
                const auto positionBits = memory.Read32OrNullopt(address + static_cast<uint32_t>(axis * 4));
                const auto rotationBits = memory.Read32OrNullopt(address + 12 + static_cast<uint32_t>(axis * 4));
                if (!positionBits || !rotationBits) {
                    mapped = false;
                    break;
                }
                std::memcpy(&position[axis], &*positionBits, sizeof(float));
                std::memcpy(&rotation[axis], &*rotationBits, sizeof(float));
            }
            if (mapped) {
                std::printf("       [%2u] pos=(%9.1f,%9.1f,%9.1f) rot=(%6.3f,%6.3f,%6.3f)\n", index, position[0],
                            position[1], position[2], rotation[0], rotation[1], rotation[2]);
            }
        }
    }

    if (!SohState_HasPlayState()) {
        std::printf("  soh: n/a (no playstate)\n");
        return;
    }
    short joints[32 * 3] = {};
    int jointCount = 0;
    int animationFrame = 0;
    int morphFrame = 0;
    const int written = SohState_ActorSkeleton(2, 0, joints, 32, &jointCount, &animationFrame, &morphFrame);
    if (written < 0) {
        std::printf("  soh: n/a (no Player actor live at title)\n");
        return;
    }
    if (written == 0) {
        std::printf("  soh: Player has no SkelAnime\n");
        return;
    }
    std::printf("  soh: Player skelAnime  limbs=%d animFrame=%d morphWeight=0x%08x\n", jointCount, animationFrame,
                static_cast<unsigned>(morphFrame));
    for (int index = 0; index < written; ++index) {
        std::printf("       [%2d] jointVec3s=(%6d,%6d,%6d)\n", index, joints[index * 3], joints[index * 3 + 1],
                    joints[index * 3 + 2]);
    }

    float eponaRotations[25 * 3] = {};
    int boneIds[25] = {};
    int parentIds[25] = {};
    char animationName[64] = {};
    float animationFrameValue = 0.0F;
    const int boneCount = SohState_AutoModelBonesLocal(2010, eponaRotations, boneIds, parentIds, 25, animationName,
                                                       sizeof(animationName), &animationFrameValue);
    if (boneCount < 0) {
        std::printf("  soh-epona: n/a (model 2010 has no live CSAB pose yet)\n");
        return;
    }
    std::printf("  soh-epona: OoT3D epona.cmb model 2010  csab=%s frame=%.3f bones=%d localRot(rad)\n", animationName,
                animationFrameValue, boneCount);
    for (int index = 0; index < boneCount; ++index) {
        std::printf("       b[%2d] parent=%2d localRot=(%7.4f,%7.4f,%7.4f)\n", boneIds[index], parentIds[index],
                    eponaRotations[index * 3], eponaRotations[index * 3 + 1], eponaRotations[index * 3 + 2]);
    }
}

} // namespace HarnessOracle
