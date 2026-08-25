// Per-model skeleton uploads and emit-ordered pose history.

#include "fast/zelda3d_pose.h"

#include "zelda3d_material_override_state.h"
#include "zelda3d_pose_interpolation.h"
#include "zelda3d_pose_state.h"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Zelda3DFast {
namespace {

struct ModelPose {
    std::vector<float> bones;
    int boneCount = 0;
    std::vector<float> bind;
    std::vector<float> inverseBind;
};

struct EmittedPose {
    std::vector<float> bones;
    int boneCount = 0;
};

std::unordered_map<int, ModelPose> modelPoses;
std::unordered_map<int, std::vector<EmittedPose>> currentPoses;
std::unordered_map<int, std::vector<EmittedPose>> previousPoses;

template <typename Map> void EvictRange(Map& models, int firstModelId, int endModelId) {
    for (auto model = models.begin(); model != models.end();) {
        if (model->first >= firstModelId && model->first < endModelId) {
            model = models.erase(model);
        } else {
            ++model;
        }
    }
}

} // namespace

PoseForDraw PoseForSubmission(int modelId, std::size_t drawIndex) {
    PoseForDraw result;
    const auto currentModel = currentPoses.find(modelId);
    if (currentModel != currentPoses.end() && drawIndex < currentModel->second.size() &&
        !currentModel->second[drawIndex].bones.empty()) {
        result.current = currentModel->second[drawIndex].bones;
        result.boneCount = currentModel->second[drawIndex].boneCount;
        const auto previousModel = previousPoses.find(modelId);
        if (previousModel != previousPoses.end() && drawIndex < previousModel->second.size() &&
            previousModel->second[drawIndex].boneCount == result.boneCount) {
            result.previous = previousModel->second[drawIndex].bones;
        }
        return result;
    }

    const auto model = modelPoses.find(modelId);
    if (model != modelPoses.end() && !model->second.bones.empty()) {
        result.current = model->second.bones;
        result.boneCount = model->second.boneCount;
    }
    return result;
}

const float* InterpolatedPose(int modelId, const PoseForDraw& pose, float step, std::vector<float>& result) {
    if (pose.current.empty()) {
        return nullptr;
    }
    if (step >= 0.999f || pose.previous.empty() || pose.previous.size() != pose.current.size()) {
        return pose.current.data();
    }

    const auto model = modelPoses.find(modelId);
    const float* bind = nullptr;
    const float* inverseBind = nullptr;
    if (model != modelPoses.end() && !model->second.bind.empty()) {
        bind = model->second.bind.data();
        inverseBind = model->second.inverseBind.data();
    }
    InterpolateSkinPose(pose.previous.data(), pose.current.data(), bind, inverseBind, step, pose.current.size(),
                        result);
    return result.data();
}

void AdvancePoseFrame() {
    previousPoses = std::move(currentPoses);
    currentPoses.clear();
}

void EvictPoses(int firstModelId, int endModelId) {
    EvictRange(modelPoses, firstModelId, endModelId);
    EvictRange(currentPoses, firstModelId, endModelId);
    EvictRange(previousPoses, firstModelId, endModelId);
}

} // namespace Zelda3DFast

extern "C" float gZelda3dInterpStep = 1.0f;

extern "C" void Zelda3D_GL_SetBones(int modelId, const float* matrices, int count) {
    auto& pose = Zelda3DFast::modelPoses[modelId];
    count = std::min(count, ZELDA3D_GL_MAX_BONES);
    if (matrices == nullptr || count <= 0) {
        pose.bones.clear();
        pose.boneCount = 0;
        return;
    }
    pose.bones.assign(matrices, matrices + static_cast<std::size_t>(count) * 16);
    pose.boneCount = count;
}

extern "C" void Zelda3D_GL_SetBoneBind(int modelId, const float* matrices, int count) {
    auto& pose = Zelda3DFast::modelPoses[modelId];
    count = std::min(count, ZELDA3D_GL_MAX_BONES);
    if (matrices == nullptr || count <= 0) {
        pose.bind.clear();
        pose.inverseBind.clear();
        return;
    }
    if (static_cast<int>(pose.bind.size()) == count * 16) {
        return;
    }
    pose.bind.assign(matrices, matrices + static_cast<std::size_t>(count) * 16);
    pose.inverseBind.resize(static_cast<std::size_t>(count) * 16);
    for (int bone = 0; bone < count; ++bone) {
        Zelda3DFast::InvertAffineMatrix(pose.bind.data() + static_cast<std::size_t>(bone) * 16,
                                        pose.inverseBind.data() + static_cast<std::size_t>(bone) * 16);
    }
}

extern "C" void Zelda3D_GL_EmitPose(int modelId) {
    Zelda3DFast::EmittedPose emitted;
    const auto model = Zelda3DFast::modelPoses.find(modelId);
    if (model != Zelda3DFast::modelPoses.end() && !model->second.bones.empty()) {
        emitted.bones = model->second.bones;
        emitted.boneCount = model->second.boneCount;
    }
    Zelda3DFast::currentPoses[modelId].push_back(std::move(emitted));
    Zelda3DFast::CaptureMaterialOverrides(modelId);
}
