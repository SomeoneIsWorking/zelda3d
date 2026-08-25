#include "zelda3d_model_internal.h"
#include "zelda3d_model_geometry.h"

#include "../render/model_queries.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
std::string CsabBaseName(const std::string& path) {
    std::string base = path;
    if (base.starts_with("Anim/")) {
        base.erase(0, 5);
    }
    if (base.ends_with(".csab")) {
        base.resize(base.size() - 5);
    }
    return base;
}

} // namespace

extern "C" const char* Zelda3D_AutoModelDefaultAnim(int modelId) {
    LoadedModel* model = loadModel(modelId);
    if (model == nullptr || !model->ok || model->zar == nullptr) {
        return nullptr;
    }
    if (!model->defaultAnimDone) {
        model->defaultAnimDone = 1;
        std::string first;
        std::string idle;
        for (const auto& file : model->zar->files()) {
            if (!file.name.ends_with(".csab")) {
                continue;
            }
            const std::string base = CsabBaseName(file.name);
            if (first.empty()) {
                first = base;
            }
            std::string lower = base;
            for (char& character : lower) {
                if (character >= 'A' && character <= 'Z') {
                    character = static_cast<char>(character + 32);
                }
            }
            if (idle.empty() && (lower.find("wait") != std::string::npos || lower.find("stand") != std::string::npos ||
                                 lower.find("matsu") != std::string::npos || lower.find("_w") != std::string::npos)) {
                idle = base;
            }
        }
        model->defaultAnim = idle.empty() ? first : idle;
        std::fprintf(stderr, "[Zelda3D] model %d default anim = '%s'\n", modelId,
                     model->defaultAnim.empty() ? "(none)" : model->defaultAnim.c_str());
    }
    return model->defaultAnim.empty() ? nullptr : model->defaultAnim.c_str();
}

extern "C" int Zelda3D_AutoModelHasCsab(int modelId, const char* base) {
    if (base == nullptr || *base == '\0') {
        return 0;
    }
    LoadedModel* model = loadModel(modelId);
    if (model == nullptr || !model->ok || model->zar == nullptr) {
        return 0;
    }

    const std::string name(base);
    const bool verbatim = name.starts_with("Anim/") || name.ends_with(".csab");
    const std::string full = verbatim ? name : "Anim/" + name + ".csab";
    for (const auto& file : model->zar->files()) {
        if (file.name == full) {
            return 1;
        }
    }
    if (!verbatim) {
        const std::string suffix = "/" + name + ".csab";
        for (const auto& file : model->zar->files()) {
            if (file.name.ends_with(suffix)) {
                return 1;
            }
        }
    }
    return 0;
}

extern "C" void Zelda3D_AutoModelCsabList(int modelId, char* out, int outSize) {
    if (out == nullptr || outSize <= 0) {
        return;
    }
    out[0] = '\0';
    LoadedModel* model = loadModel(modelId);
    if (model == nullptr || model->zar == nullptr) {
        return;
    }

    int position = 0;
    for (const auto& file : model->zar->files()) {
        if (!file.name.ends_with(".csab")) {
            continue;
        }
        const std::string base = CsabBaseName(file.name);
        int duration = -1;
        if (model->cmb != nullptr) {
            Zelda3D::Csab csab(model->zar->read(file));
            if (csab.ok()) {
                duration = static_cast<int>(csab.duration());
            }
        }
        const int written = std::snprintf(out + position, static_cast<size_t>(outSize - position), "%s%s(%d)",
                                          position != 0 ? " " : "", base.c_str(), duration);
        if (written <= 0 || written >= outSize - position) {
            break;
        }
        position += written;
    }
}

