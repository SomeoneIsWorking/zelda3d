#include "mm3d_player_sheath_policy.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Zelda3D::MM3D {
namespace {

constexpr int kNoMesh = -1;

// MM3D .code 0x0069144c..0x006914c3. Rows are PlayerModelType
// SHEATH_12..SHEATH_15; columns are the five Player forms. Retail stores two
// identical LOD entries for every cell, collapsed here to one mesh ID.
constexpr std::array<std::array<int, 5>, 4> kSheathMeshByTypeAndForm = { {
    { kNoMesh, kNoMesh, kNoMesh, 8, 5 },
    { kNoMesh, kNoMesh, kNoMesh, 8, 5 },
    { kNoMesh, kNoMesh, kNoMesh, kNoMesh, kNoMesh },
    { kNoMesh, kNoMesh, kNoMesh, kNoMesh, kNoMesh },
} };

// MM3D .code 0x006914d4 and 0x006914f4. Human replaces the generic sheath
// entry with a sword-specific group. Index zero is retail's absent sentinel.
constexpr std::array<int, 4> kSheathedSwordMeshes = { kNoMesh, 5, 6, 7 };
constexpr std::array<int, 4> kEmptySheathMeshes = { kNoMesh, 13, 15, 17 };

constexpr std::uint64_t MeshBit(int meshId) {
    return meshId < 0 ? 0 : std::uint64_t{ 1 } << meshId;
}

constexpr std::size_t Index(PlayerSheathType type) {
    return static_cast<std::size_t>(type);
}

constexpr std::size_t Index(PlayerSword sword) {
    return static_cast<std::size_t>(sword);
}

} // namespace

bool PlayerSwordFromRetailIndex(int value, PlayerSword& result) {
    switch (value) {
        case 0:
            result = PlayerSword::None;
            return true;
        case 1:
            result = PlayerSword::Kokiri;
            return true;
        case 2:
            result = PlayerSword::Razor;
            return true;
        case 3:
            result = PlayerSword::Gilded;
            return true;
        default:
            return false;
    }
}

std::uint64_t PlayerSheathMeshMaskForState(const PlayerSheathState& state) {
    // 0x0020d0ec branches around both the back-shield and sheath selection
    // when the Giant's Mask is active.
    if (state.giantMask) {
        return 0;
    }

    if (state.form != PlayerModelForm::Human) {
        const int meshId =
            kSheathMeshByTypeAndForm.at(Index(state.sheathType)).at(static_cast<std::size_t>(state.form));
        return MeshBit(meshId);
    }

    std::uint64_t mask = 0;
    if (state.sheathType == PlayerSheathType::Type14 || state.sheathType == PlayerSheathType::Type15) {
        if (state.shield == PlayerShield::Hero) {
            mask |= MeshBit(3);
        } else if (state.shield == PlayerShield::Mirror) {
            mask |= MeshBit(4);
        }
    }

    const bool swordIsSheathed =
        state.sheathType == PlayerSheathType::Type12 || state.sheathType == PlayerSheathType::Type14;
    const auto& meshes = swordIsSheathed ? kSheathedSwordMeshes : kEmptySheathMeshes;
    return mask | MeshBit(meshes.at(Index(state.sword)));
}

} // namespace Zelda3D::MM3D
