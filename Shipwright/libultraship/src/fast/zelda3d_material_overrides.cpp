// Per-model material overrides and the emit-ordered snapshots consumed by deferred draws.

#include "fast/zelda3d_material_overrides.h"

#include "zelda3d_material_override_state.h"

#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Zelda3DFast {
namespace {

std::unordered_map<int, MaterialOverrides> pendingOverrides;
std::unordered_map<int, std::vector<MaterialOverrides>> emittedOverrides;

} // namespace

void CaptureMaterialOverrides(int modelId) {
    const auto pending = pendingOverrides.find(modelId);
    emittedOverrides[modelId].push_back(pending == pendingOverrides.end() ? MaterialOverrides{} : pending->second);
}

MaterialOverrides MaterialOverridesForDraw(int modelId, std::size_t drawIndex) {
    const auto modelOverrides = emittedOverrides.find(modelId);
    if (modelOverrides == emittedOverrides.end() || drawIndex >= modelOverrides->second.size()) {
        return {};
    }
    return modelOverrides->second[drawIndex];
}

void BeginMaterialOverrideFrame() {
    emittedOverrides.clear();
}

void EvictMaterialOverrides(int firstModelId, int endModelId) {
    for (auto override = pendingOverrides.begin(); override != pendingOverrides.end();) {
        if (override->first >= firstModelId && override->first < endModelId) {
            override = pendingOverrides.erase(override);
        } else {
            ++override;
        }
    }
    for (auto override = emittedOverrides.begin(); override != emittedOverrides.end();) {
        if (override->first >= firstModelId && override->first < endModelId) {
            override = emittedOverrides.erase(override);
        } else {
            ++override;
        }
    }
}

} // namespace Zelda3DFast

extern "C" void Zelda3D_GL_SetMidMask(int modelId, unsigned long long mask) {
    Zelda3DFast::pendingOverrides[modelId].visibleMeshMask = mask;
}

extern "C" void Zelda3D_GL_SetMatTexOverride(int modelId, int materialIndex, int texIndex) {
    auto& textures = Zelda3DFast::pendingOverrides[modelId].textures;
    if (texIndex < 0) {
        textures.erase(materialIndex);
    } else {
        textures[materialIndex] = texIndex;
    }
}

extern "C" void Zelda3D_GL_ClearMatTexOverrides(int modelId) {
    const auto model = Zelda3DFast::pendingOverrides.find(modelId);
    if (model != Zelda3DFast::pendingOverrides.end()) {
        model->second.textures.clear();
    }
}

extern "C" void Zelda3D_GL_SetMatConstOverride(int modelId, int materialIndex, int constIdx, float red, float green,
                                               float blue, float alpha) {
    if (materialIndex < 0 || constIdx < 0 || constIdx >= 6) {
        return;
    }
    Zelda3DMatConstOv override{};
    override.constIdx = constIdx;
    override.rgba[0] = red;
    override.rgba[1] = green;
    override.rgba[2] = blue;
    override.rgba[3] = alpha;
    Zelda3DFast::pendingOverrides[modelId].constants[materialIndex * 6 + constIdx] = override;

    static const bool debugEnabled = [] {
        const char* value = std::getenv("ZELDA3D_DBG_MATCONST");
        return value != nullptr && value[0] != '\0';
    }();
    if (debugEnabled) {
        std::fprintf(stderr, "[MATCONST] model=%d mat=%d constIdx=%d rgba=(%.3f,%.3f,%.3f,%.3f)\n", modelId,
                     materialIndex, constIdx, red, green, blue, alpha);
    }
}

extern "C" void Zelda3D_GL_ClearMatConstOverrides(int modelId) {
    const auto model = Zelda3DFast::pendingOverrides.find(modelId);
    if (model != Zelda3DFast::pendingOverrides.end()) {
        model->second.constants.clear();
    }
}

extern "C" void Zelda3D_GL_SetMatUvOverride(int modelId, int materialIndex, float u, float v) {
    Zelda3DFast::pendingOverrides[modelId].textureCoordinates[materialIndex] = { u, v };
}

extern "C" void Zelda3D_GL_ClearMatUvOverrides(int modelId) {
    const auto model = Zelda3DFast::pendingOverrides.find(modelId);
    if (model != Zelda3DFast::pendingOverrides.end()) {
        model->second.textureCoordinates.clear();
    }
}
