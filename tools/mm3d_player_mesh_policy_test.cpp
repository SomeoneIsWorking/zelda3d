#include "2s2h/zelda3d/mm3d_player_mesh_policy.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>

namespace {

std::uint64_t Mask(std::initializer_list<int> meshIds) {
    std::uint64_t mask = 0;
    for (const int meshId : meshIds) {
        mask |= std::uint64_t{ 1 } << meshId;
    }
    return mask;
}

} // namespace

int main() {
    using Zelda3D::MM3D::PlayerBaseMeshMaskForForm;
    using Zelda3D::MM3D::PlayerModelForm;

    assert(PlayerBaseMeshMaskForForm(PlayerModelForm::FierceDeity) == Mask({ 9, 10, 12 }));
    assert(PlayerBaseMeshMaskForForm(PlayerModelForm::Goron) == Mask({ 6, 7, 10 }));
    assert(PlayerBaseMeshMaskForForm(PlayerModelForm::Zora) == Mask({ 6, 7 }));
    assert(PlayerBaseMeshMaskForForm(PlayerModelForm::Deku) == Mask({ 5, 9, 10, 11, 13 }));
    assert(PlayerBaseMeshMaskForForm(PlayerModelForm::Human) == Mask({ 28, 29, 30, 32, 33 }));
}
