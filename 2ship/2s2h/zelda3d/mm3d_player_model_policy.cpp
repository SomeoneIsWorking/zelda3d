#include "mm3d_player_model_policy.h"

#include <array>
#include <cstddef>

namespace Zelda3D::MM3D {
namespace {

constexpr std::array<PlayerModelAsset, 5> kPlayerModels = { {
    { "/actors/zelda2_link_boy_new.gar.lzs", "link_demon" },
    { "/actors/zelda2_link_goron_new.gar.lzs", "link_goron" },
    { "/actors/zelda2_link_zora_new.gar.lzs", "link_zora" },
    { "/actors/zelda2_link_nuts_new.gar.lzs", "link_deknuts" },
    { "/actors/zelda2_link_child_new.gar.lzs", "link_child" },
} };

} // namespace

bool PlayerModelFormFromRetailIndex(int formIndex, PlayerModelForm& result) {
    if (formIndex < static_cast<int>(PlayerModelForm::FierceDeity) ||
        formIndex > static_cast<int>(PlayerModelForm::Human)) {
        return false;
    }
    result = static_cast<PlayerModelForm>(formIndex);
    return true;
}

const PlayerModelAsset& PlayerModelAssetForForm(PlayerModelForm form) {
    return kPlayerModels.at(static_cast<std::size_t>(form));
}

} // namespace Zelda3D::MM3D
