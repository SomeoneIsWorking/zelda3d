#include "zelda3d_model_geometry.h"
#include "zelda3d_model_internal.h"

#include <algorithm>
#include <cstdio>

extern "C" void Zelda3D_DumpModelBones(int modelId) {
    LoadedModel* model = loadModel(modelId);
    if (model == nullptr || !model->ok || model->cmb == nullptr) {
        std::fprintf(stderr, "[SKELDUMP] OOT3D model %d: no cmb\n", modelId);
        return;
    }

    const auto& bones = model->cmb->bones();
    const auto& matrices = model->cmb->boneMatrices();
    float minimumY = 1e30f;
    float maximumY = -1e30f;
    for (const auto& bone : bones) {
        if (bone.id >= 0 && static_cast<size_t>(bone.id) < matrices.size()) {
            minimumY = std::min(minimumY, matrices[bone.id][7]);
            maximumY = std::max(maximumY, matrices[bone.id][7]);
        }
    }
    const float boneSpanY = minimumY <= maximumY ? maximumY - minimumY : 0.0f;
    std::fprintf(stderr, "[SKELDUMP] OOT3D model %d: %zu bones meshH=%.2f boneSpanY=%.2f\n", modelId,
                 bones.size(), Zelda3D_ModelGeometryHeight(*model), boneSpanY);
    for (const auto& bone : bones) {
        float worldX = 0.0f;
        float worldY = 0.0f;
        float worldZ = 0.0f;
        if (bone.id >= 0 && static_cast<size_t>(bone.id) < matrices.size()) {
            worldX = matrices[bone.id][3];
            worldY = matrices[bone.id][7];
            worldZ = matrices[bone.id][11];
        }
        std::fprintf(stderr,
                     "[SKELDUMP] OOT3D b id=%d parent=%d trans=(%.3f,%.3f,%.3f) rot=(%.4f,%.4f,%.4f) "
                     "scale=(%.3f,%.3f,%.3f) world=(%.2f,%.2f,%.2f)\n",
                     bone.id, bone.parent, bone.trans[0], bone.trans[1], bone.trans[2], bone.rot[0], bone.rot[1],
                     bone.rot[2], bone.scale[0], bone.scale[1], bone.scale[2], worldX, worldY, worldZ);
    }
    std::fflush(stderr);
}
