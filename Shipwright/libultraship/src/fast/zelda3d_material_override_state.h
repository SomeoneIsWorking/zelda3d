// Internal per-model material override snapshots captured at pose-emission time.
#ifndef ZELDA3D_FAST_MATERIAL_OVERRIDE_STATE_H
#define ZELDA3D_FAST_MATERIAL_OVERRIDE_STATE_H

#include "fast/zelda3d_material_overrides.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace Zelda3DFast {

struct MaterialOverrides {
    std::uint64_t visibleMeshMask = ~std::uint64_t{ 0 };
    std::unordered_map<int, int> textures;
    std::unordered_map<int, Zelda3DMatConstOv> constants;
    std::unordered_map<int, Zelda3DMatUvOv> textureCoordinates;
};

void CaptureMaterialOverrides(int modelId);
MaterialOverrides MaterialOverridesForDraw(int modelId, std::size_t drawIndex);
void BeginMaterialOverrideFrame();
void EvictMaterialOverrides(int firstModelId, int endModelId);

} // namespace Zelda3DFast

#endif // ZELDA3D_FAST_MATERIAL_OVERRIDE_STATE_H
