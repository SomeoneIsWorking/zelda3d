// Rigid-aware interpolation for Zelda3D skin matrices.

#include "zelda3d_pose_interpolation.h"

#include <algorithm>
#include <cmath>

namespace Zelda3DFast {
namespace {

// Row-major 4x4 multiply (M*v column-vector convention, same as the OoT3D asset code): C = A*B.
void MultiplyMatrix(const float* lhs, const float* rhs, float* result) {
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            float value = 0.0f;
            for (int term = 0; term < 4; ++term) {
                value += lhs[row * 4 + term] * rhs[term * 4 + column];
            }
            result[row * 4 + column] = value;
        }
    }
}

// Extract rotation quaternion (x,y,z,w) and per-column scale from a row-major affine matrix.
// Degenerate and reflected transforms cannot be interpolated safely.
bool DecomposeRotationScale(const float* matrix, float quaternion[4], float scale[3]) {
    float columns[3][3];
    for (int column = 0; column < 3; ++column) {
        columns[column][0] = matrix[column];
        columns[column][1] = matrix[4 + column];
        columns[column][2] = matrix[8 + column];
    }
    for (int column = 0; column < 3; ++column) {
        scale[column] = std::sqrt(columns[column][0] * columns[column][0] + columns[column][1] * columns[column][1] +
                                  columns[column][2] * columns[column][2]);
    }
    if (scale[0] < 1e-8f || scale[1] < 1e-8f || scale[2] < 1e-8f) {
        return false;
    }

    float rotation[9];
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            rotation[row * 3 + column] = columns[column][row] / scale[column];
        }
    }
    const float determinant = rotation[0] * (rotation[4] * rotation[8] - rotation[5] * rotation[7]) -
                              rotation[1] * (rotation[3] * rotation[8] - rotation[5] * rotation[6]) +
                              rotation[2] * (rotation[3] * rotation[7] - rotation[4] * rotation[6]);
    if (determinant < 0.0f) {
        return false;
    }

    const float trace = rotation[0] + rotation[4] + rotation[8];
    if (trace > 0.0f) {
        const float scaleFactor = std::sqrt(trace + 1.0f) * 2.0f;
        quaternion[3] = 0.25f * scaleFactor;
        quaternion[0] = (rotation[7] - rotation[5]) / scaleFactor;
        quaternion[1] = (rotation[2] - rotation[6]) / scaleFactor;
        quaternion[2] = (rotation[3] - rotation[1]) / scaleFactor;
    } else if (rotation[0] > rotation[4] && rotation[0] > rotation[8]) {
        const float scaleFactor = std::sqrt(1.0f + rotation[0] - rotation[4] - rotation[8]) * 2.0f;
        quaternion[3] = (rotation[7] - rotation[5]) / scaleFactor;
        quaternion[0] = 0.25f * scaleFactor;
        quaternion[1] = (rotation[1] + rotation[3]) / scaleFactor;
        quaternion[2] = (rotation[2] + rotation[6]) / scaleFactor;
    } else if (rotation[4] > rotation[8]) {
        const float scaleFactor = std::sqrt(1.0f + rotation[4] - rotation[0] - rotation[8]) * 2.0f;
        quaternion[3] = (rotation[2] - rotation[6]) / scaleFactor;
        quaternion[0] = (rotation[1] + rotation[3]) / scaleFactor;
        quaternion[1] = 0.25f * scaleFactor;
        quaternion[2] = (rotation[5] + rotation[7]) / scaleFactor;
    } else {
        const float scaleFactor = std::sqrt(1.0f + rotation[8] - rotation[0] - rotation[4]) * 2.0f;
        quaternion[3] = (rotation[3] - rotation[1]) / scaleFactor;
        quaternion[0] = (rotation[2] + rotation[6]) / scaleFactor;
        quaternion[1] = (rotation[5] + rotation[7]) / scaleFactor;
        quaternion[2] = 0.25f * scaleFactor;
    }
    return true;
}

void CopyMatrix(const float* source, float* destination) {
    std::copy_n(source, 16, destination);
}

// Interpolate a row-major affine matrix by decomposing it into rotation, scale, and translation.
void InterpolateRigid(const float* previous, const float* current, float step, float* result) {
    float previousQuaternion[4];
    float currentQuaternion[4];
    float previousScale[3];
    float currentScale[3];
    if (!DecomposeRotationScale(previous, previousQuaternion, previousScale) ||
        !DecomposeRotationScale(current, currentQuaternion, currentScale)) {
        CopyMatrix(current, result);
        return;
    }

    const float dot = previousQuaternion[0] * currentQuaternion[0] + previousQuaternion[1] * currentQuaternion[1] +
                      previousQuaternion[2] * currentQuaternion[2] + previousQuaternion[3] * currentQuaternion[3];
    const float currentSign = dot < 0.0f ? -1.0f : 1.0f;
    float quaternion[4];
    for (int component = 0; component < 4; ++component) {
        quaternion[component] =
            (1.0f - step) * previousQuaternion[component] + step * currentSign * currentQuaternion[component];
    }
    const float quaternionLength = std::sqrt(quaternion[0] * quaternion[0] + quaternion[1] * quaternion[1] +
                                             quaternion[2] * quaternion[2] + quaternion[3] * quaternion[3]);
    if (quaternionLength < 1e-8f) {
        CopyMatrix(current, result);
        return;
    }
    for (float& component : quaternion) {
        component /= quaternionLength;
    }

    float scale[3];
    for (int component = 0; component < 3; ++component) {
        scale[component] = (1.0f - step) * previousScale[component] + step * currentScale[component];
    }
    const float x = quaternion[0];
    const float y = quaternion[1];
    const float z = quaternion[2];
    const float w = quaternion[3];
    const float rotation[9] = {
        1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - w * z),        2.0f * (x * z + w * y),
        2.0f * (x * y + w * z),        1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - w * x),
        2.0f * (x * z - w * y),        2.0f * (y * z + w * x),        1.0f - 2.0f * (x * x + y * y),
    };
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            result[row * 4 + column] = rotation[row * 3 + column] * scale[column];
        }
    }
    result[3] = (1.0f - step) * previous[3] + step * current[3];
    result[7] = (1.0f - step) * previous[7] + step * current[7];
    result[11] = (1.0f - step) * previous[11] + step * current[11];
    result[12] = 0.0f;
    result[13] = 0.0f;
    result[14] = 0.0f;
    result[15] = 1.0f;
}

} // namespace

void InvertAffineMatrix(const float* matrix, float* inverse) {
    double augmented[4][8];
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            augmented[row][column] = matrix[row * 4 + column];
            augmented[row][4 + column] = row == column ? 1.0 : 0.0;
        }
    }
    for (int column = 0; column < 4; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 4; ++row) {
            if (std::fabs(augmented[row][column]) > std::fabs(augmented[pivot][column])) {
                pivot = row;
            }
        }
        if (std::fabs(augmented[pivot][column]) < 1e-12) {
            for (int index = 0; index < 16; ++index) {
                inverse[index] = index % 5 == 0 ? 1.0f : 0.0f;
            }
            return;
        }
        for (int item = 0; item < 8; ++item) {
            std::swap(augmented[column][item], augmented[pivot][item]);
        }
        const double divisor = augmented[column][column];
        for (double& item : augmented[column]) {
            item /= divisor;
        }
        for (int row = 0; row < 4; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = augmented[row][column];
            for (int item = 0; item < 8; ++item) {
                augmented[row][item] -= factor * augmented[column][item];
            }
        }
    }
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            inverse[row * 4 + column] = static_cast<float>(augmented[row][4 + column]);
        }
    }
}

void InterpolateSkinPose(const float* previous, const float* current, const float* bind, const float* inverseBind,
                         float step, std::size_t matrixValueCount, std::vector<float>& result) {
    result.resize(matrixValueCount);
    const float interpolation = std::clamp(step, 0.0f, 1.0f);
    const std::size_t boneCount = matrixValueCount / 16;
    if (bind == nullptr || inverseBind == nullptr) {
        std::copy_n(current, matrixValueCount, result.data());
        return;
    }

    std::vector<float> previousWorld(matrixValueCount);
    std::vector<float> currentWorld(matrixValueCount);
    bool discontinuous = false;
    for (std::size_t bone = 0; bone < boneCount; ++bone) {
        const float* bindMatrix = bind + bone * 16;
        MultiplyMatrix(previous + bone * 16, bindMatrix, previousWorld.data() + bone * 16);
        MultiplyMatrix(current + bone * 16, bindMatrix, currentWorld.data() + bone * 16);
        float previousQuaternion[4];
        float currentQuaternion[4];
        float previousScale[3];
        float currentScale[3];
        if (DecomposeRotationScale(previousWorld.data() + bone * 16, previousQuaternion, previousScale) &&
            DecomposeRotationScale(currentWorld.data() + bone * 16, currentQuaternion, currentScale)) {
            const float dot =
                std::fabs(previousQuaternion[0] * currentQuaternion[0] + previousQuaternion[1] * currentQuaternion[1] +
                          previousQuaternion[2] * currentQuaternion[2] + previousQuaternion[3] * currentQuaternion[3]);
            if (dot < 0.707f) {
                discontinuous = true;
            }
        }
    }
    if (discontinuous) {
        std::copy_n(current, matrixValueCount, result.data());
        return;
    }

    for (std::size_t bone = 0; bone < boneCount; ++bone) {
        float interpolatedWorld[16];
        InterpolateRigid(previousWorld.data() + bone * 16, currentWorld.data() + bone * 16, interpolation,
                         interpolatedWorld);
        MultiplyMatrix(interpolatedWorld, inverseBind + bone * 16, result.data() + bone * 16);
    }
}

} // namespace Zelda3DFast
