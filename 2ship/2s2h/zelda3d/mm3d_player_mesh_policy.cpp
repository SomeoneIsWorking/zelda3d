#include "mm3d_player_mesh_policy.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Zelda3D::MM3D {
namespace {

// MM3D .code VA 0x0020cfa4 copies the five-by-five table at 0x00626b5c,
// indexed by Player form, then enables these mesh IDs while disabling every
// other group. -1 is retail's absent-slot sentinel. See
// mm3d-decomp/docs/player_draw.md.
constexpr int kNoMesh = -1;
constexpr std::array<std::array<int, 5>, 5> kBaseMeshIdsByForm = { {
    { 10, 12, 9, kNoMesh, kNoMesh },
    { 7, 6, 10, kNoMesh, kNoMesh },
    { 6, 7, kNoMesh, kNoMesh, kNoMesh },
    { 5, 9, 13, 10, 11 },
    { 28, 29, 33, 30, 32 },
} };

} // namespace

std::uint64_t PlayerBaseMeshMaskForForm(PlayerModelForm form) {
    std::uint64_t mask = 0;
    for (const int meshId : kBaseMeshIdsByForm.at(static_cast<std::size_t>(form))) {
        if (meshId >= 0) {
            mask |= std::uint64_t{ 1 } << meshId;
        }
    }
    return mask;
}

} // namespace Zelda3D::MM3D
