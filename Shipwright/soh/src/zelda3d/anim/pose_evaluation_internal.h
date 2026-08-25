// Animation-runtime inputs shared with the authored-pose evaluator.
#ifndef ZELDA3D_ANIM_POSE_EVALUATION_INTERNAL_H
#define ZELDA3D_ANIM_POSE_EVALUATION_INTERNAL_H

#include "pose_evaluation.h"

#include "asset/csab.h"

#include <array>
#include <vector>

struct LoadedModel;

struct Zelda3D_AuthoredPoseInputs {
    LoadedModel* model = nullptr;
    Zelda3D::Csab* animation = nullptr;
    const float* rotationDeltas = nullptr;
    int rotationDeltaCount = 0;
    const float* postRotations = nullptr;
    int postRotationCount = 0;
    Zelda3D::RootMotion rootMotion = {};
};

// Resolve the animation runtime's cached clip and live procedural channels without duplicating
// their storage policy in the pose evaluator.
bool Zelda3D_ResolveAuthoredPoseInputs(int modelId, const char* animName, Zelda3D_AuthoredPoseInputs* outInputs);
bool Zelda3D_EvaluateAnimatedBoneWorld(int modelId, const char* animName, float frame, LoadedModel** outModel,
                                       std::vector<std::array<float, 16>>& outWorld);

#endif // ZELDA3D_ANIM_POSE_EVALUATION_INTERNAL_H
