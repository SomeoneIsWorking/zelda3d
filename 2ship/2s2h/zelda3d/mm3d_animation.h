#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_MM_CaptureAnimState(void* jointTable, void* animation, float curFrame, float animLength,
                                 float morphWeight);

#ifdef __cplusplus
}

namespace Zelda3D::MM3D {

struct AnimationResetCounts {
    size_t capturedStates;
    size_t playheads;
};

const char* ApplyCapturedAnimation(int modelId, const void* jointTable);
AnimationResetCounts ResetAnimationState();

} // namespace Zelda3D::MM3D
#endif
