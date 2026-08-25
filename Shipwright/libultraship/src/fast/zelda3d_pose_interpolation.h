// Rigid-aware interpolation for Zelda3D skin matrices.
#ifndef ZELDA3D_FAST_POSE_INTERPOLATION_H
#define ZELDA3D_FAST_POSE_INTERPOLATION_H

#include <cstddef>
#include <vector>

namespace Zelda3DFast {

void InvertAffineMatrix(const float* matrix, float* inverse);
void InterpolateSkinPose(const float* previous, const float* current, const float* bind, const float* inverseBind,
                         float step, std::size_t matrixValueCount, std::vector<float>& result);

} // namespace Zelda3DFast

#endif // ZELDA3D_FAST_POSE_INTERPOLATION_H
