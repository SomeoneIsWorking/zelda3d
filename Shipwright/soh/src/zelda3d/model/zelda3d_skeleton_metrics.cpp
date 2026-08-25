#include "zelda3d_model_internal.h"

#include "../render/model_queries.h"

#include <cmath>

extern "C" int Zelda3D_AutoModelBoneCount(int modelId) {
    LoadedModel* model = loadModel(modelId);
    return model != nullptr && model->ok && model->cmb != nullptr ? static_cast<int>(model->cmb->bones().size())
                                                                  : 0;
}

extern "C" float Zelda3D_AutoModelBoneLenSum(int modelId, int boneCap) {
    (void)boneCap; // Capping at limbCount regressed rigs whose authored skeleton uses extra bones.
    LoadedModel* model = loadModel(modelId);
    if (model == nullptr || !model->ok || model->cmb == nullptr) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (const auto& bone : model->cmb->bones()) {
        if (bone.parent < 0) {
            continue;
        }
        sum += std::sqrt(bone.trans[0] * bone.trans[0] + bone.trans[1] * bone.trans[1] +
                         bone.trans[2] * bone.trans[2]);
    }
    return sum;
}
