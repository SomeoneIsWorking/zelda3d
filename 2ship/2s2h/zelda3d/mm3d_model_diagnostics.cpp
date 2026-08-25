// MM3D model diagnostics: reports catalog entries and geometry-derived scale metrics.
#include "mm3d_model_diagnostics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "mm3d_model.h"
#include "mm3d_model_catalog.h"
#include "mm3d_model_store.h"

extern "C" {

float Zelda3D_MM_ModelBoneLenSum(int modelId) {
    Zelda3D::MM3D::LoadedModel* model = Zelda3D::MM3D::LoadModel(modelId);
    if (model == nullptr || !model->ok || model->cmb == nullptr) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (const auto& bone : model->cmb->bones()) {
        if (bone.parent >= 0) {
            sum += std::sqrt(bone.trans[0] * bone.trans[0] + bone.trans[1] * bone.trans[1] +
                             bone.trans[2] * bone.trans[2]);
        }
    }
    return sum;
}

float Zelda3D_MM_ModelMinY(int modelId) {
    Zelda3D::MM3D::LoadedModel* model = Zelda3D::MM3D::LoadModel(modelId);
    if (model == nullptr || !model->ok) {
        return 0.0f;
    }
    float minimum = 1e30f;
    for (const auto& group : model->groups) {
        for (const auto& vertex : group.verts) {
            minimum = std::min(minimum, vertex.pos[1]);
        }
    }
    return minimum < 1e29f ? minimum : 0.0f;
}

void Zelda3D_MM_DumpModelBones(int modelId, int count) {
    Zelda3D::MM3D::LoadedModel* model = Zelda3D::MM3D::LoadModel(modelId);
    if (model == nullptr || !model->ok || model->cmb == nullptr) {
        return;
    }
    int index = 0;
    for (const auto& bone : model->cmb->bones()) {
        if (index >= count) {
            break;
        }
        if (bone.parent >= 0) {
            const float length = std::sqrt(bone.trans[0] * bone.trans[0] + bone.trans[1] * bone.trans[1] +
                                           bone.trans[2] * bone.trans[2]);
            fprintf(stderr, "[MM3D-BONE-CMB] model=%d bone=%d trans=(%.1f,%.1f,%.1f) |v|=%.2f\n", modelId, index,
                    bone.trans[0], bone.trans[1], bone.trans[2], length);
        }
        ++index;
    }
}

void Zelda3D_ListModels(void (*emitLine)(const char* line, void* user), void* user) {
    if (emitLine == nullptr) {
        return;
    }
    std::vector<Zelda3D::MM3D::CatalogEntry> entries = Zelda3D::MM3D::CatalogSnapshot();
    std::sort(entries.begin(), entries.end(),
              [](const auto& left, const auto& right) { return left.objectId < right.objectId; });
    char line[192];
    for (const auto& entry : entries) {
        snprintf(line, sizeof(line), "  obj=0x%03X (%s) -> modelId=%d scale=%.4f", entry.objectId,
                 entry.objectName.c_str(), entry.modelId, entry.worldScale);
        emitLine(line, user);
    }
    if (entries.empty()) {
        emitLine("  (no auto-mapped MM3D models yet)", user);
    }
}

} // extern "C"
