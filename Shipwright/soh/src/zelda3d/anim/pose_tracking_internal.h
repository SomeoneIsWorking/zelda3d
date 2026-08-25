// Internal seam from animation execution to posed-geometry tracking.
#ifndef ZELDA3D_ANIM_POSE_TRACKING_INTERNAL_H
#define ZELDA3D_ANIM_POSE_TRACKING_INTERNAL_H

#include "pose_tracking.h"

#include <array>
#include <vector>

void Zelda3D_CacheTrackedPose(int modelId, const std::vector<std::array<float, 16>>& skinMatrices);

#endif // ZELDA3D_ANIM_POSE_TRACKING_INTERNAL_H
