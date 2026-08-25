// Authored-pose queries used by behavior-owned procedural placement.
#include "pose_evaluation_internal.h"

#include "../model/zelda3d_model_internal.h"

#include <algorithm>

bool Zelda3D_EvaluateAnimatedBoneWorld(int modelId, const char* animName, float frame, LoadedModel** outModel,
                                       std::vector<std::array<float, 16>>& outWorld) {
    outWorld.clear();
    if (outModel)
        *outModel = nullptr;
    Zelda3D_AuthoredPoseInputs inputs;
    if (!Zelda3D_ResolveAuthoredPoseInputs(modelId, animName, &inputs))
        return false;
    inputs.animation->animatedBoneWorld(*inputs.model->cmb, frame, outWorld, inputs.rotationDeltas,
                                        inputs.rotationDeltaCount, inputs.postRotations, inputs.postRotationCount,
                                        inputs.rootMotion);
    if (outModel)
        *outModel = inputs.model;
    return true;
}

extern "C" int Zelda3D_AnimWorldBone(int modelId, const char* animName, float frame, int boneIndex,
                                     float* outMatrix4x4) {
    if (boneIndex < 0 || !outMatrix4x4)
        return 0;
    std::vector<std::array<float, 16>> world;
    if (!Zelda3D_EvaluateAnimatedBoneWorld(modelId, animName, frame, nullptr, world) ||
        boneIndex >= static_cast<int>(world.size())) {
        return 0;
    }
    std::copy(world[boneIndex].begin(), world[boneIndex].end(), outMatrix4x4);
    return 1;
}
