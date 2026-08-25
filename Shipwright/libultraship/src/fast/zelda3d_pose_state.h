// Internal emit-ordered pose history consumed by the deferred submission path.
#ifndef ZELDA3D_FAST_POSE_STATE_H
#define ZELDA3D_FAST_POSE_STATE_H

#include <cstddef>
#include <vector>

namespace Zelda3DFast {

struct PoseForDraw {
    std::vector<float> current;
    std::vector<float> previous;
    int boneCount = 0;
};

PoseForDraw PoseForSubmission(int modelId, std::size_t drawIndex);
const float* InterpolatedPose(int modelId, const PoseForDraw& pose, float step, std::vector<float>& result);
void AdvancePoseFrame();
void EvictPoses(int firstModelId, int endModelId);

} // namespace Zelda3DFast

#endif // ZELDA3D_FAST_POSE_STATE_H
