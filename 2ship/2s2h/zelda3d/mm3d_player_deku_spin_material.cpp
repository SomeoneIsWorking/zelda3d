#include "mm3d_player_deku_spin_material.h"

#include <cstddef>

#include "mm3d_player_deku_spin_material_policy.h"
#include "mm3d_player_model_policy.h"

extern "C" void Player_Action_95(Player* player, PlayState* play);

extern "C" int Zelda3D_MM_PlayerDekuSpinMaterialOverride(const Player* player,
                                                         Zelda3DMMPlayerDekuSpinMaterialOverride* materialOverride) {
    using namespace Zelda3D::MM3D;
    if (player == nullptr || materialOverride == nullptr) {
        return 0;
    }

    PlayerModelForm form = PlayerModelForm::Human;
    if (!PlayerModelFormFromRetailIndex(player->transformation, form)) {
        return 0;
    }

    *materialOverride = {};
    const PlayerDekuSpinMaterialState state{
        form,
        player->actionFunc == Player_Action_95,
        player->unk_B10[1],
    };
    const auto override = PlayerDekuSpinMaterialOverrideForState(state);
    if (!override.has_value()) {
        return 1;
    }

    materialOverride->enabled = 1;
    materialOverride->materialIndex = override->materialIndex;
    materialOverride->constantIndex = override->constantIndex;
    for (std::size_t component = 0; component < override->rgba.size(); ++component) {
        materialOverride->rgba[component] = override->rgba[component];
    }
    return 1;
}
