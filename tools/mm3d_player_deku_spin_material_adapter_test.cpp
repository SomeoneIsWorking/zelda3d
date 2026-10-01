#include "2s2h/zelda3d/mm3d_player_deku_spin_material.h"

#include <cassert>
#include <cmath>

extern "C" void Player_Action_95(Player*, PlayState*) {}

namespace {

bool Near(float actual, float expected) {
    return std::fabs(actual - expected) < 1e-6F;
}

} // namespace

int main() {
    Player player{};
    Zelda3DMMPlayerDekuSpinMaterialOverride override{};

    player.transformation = PLAYER_FORM_HUMAN;
    assert(Zelda3D_MM_PlayerDekuSpinMaterialOverride(&player, &override));
    assert(!override.enabled);

    player.transformation = PLAYER_FORM_DEKU;
    player.unk_B10[1] = 100000.0F;
    assert(Zelda3D_MM_PlayerDekuSpinMaterialOverride(&player, &override));
    assert(override.enabled);
    assert(override.materialIndex == 6);
    assert(override.constantIndex == 4);
    assert(Near(override.rgba[3], 0.0F));

    player.actionFunc = Player_Action_95;
    assert(Zelda3D_MM_PlayerDekuSpinMaterialOverride(&player, &override));
    assert(override.enabled);
    assert(Near(override.rgba[3], 1.0F));

    assert(!Zelda3D_MM_PlayerDekuSpinMaterialOverride(nullptr, &override));
    assert(!Zelda3D_MM_PlayerDekuSpinMaterialOverride(&player, nullptr));
}
