#include "mm3d_player_sheath.h"

#include "global.h"
#include "mm3d_player_model_policy.h"
#include "mm3d_player_sheath_policy.h"

namespace Zelda3D::MM3D {
namespace {

bool ToPlayerSheathType(int value, PlayerSheathType& result) {
    switch (value) {
        case PLAYER_MODELTYPE_SHEATH_12:
            result = PlayerSheathType::Type12;
            return true;
        case PLAYER_MODELTYPE_SHEATH_13:
            result = PlayerSheathType::Type13;
            return true;
        case PLAYER_MODELTYPE_SHEATH_14:
            result = PlayerSheathType::Type14;
            return true;
        case PLAYER_MODELTYPE_SHEATH_15:
            result = PlayerSheathType::Type15;
            return true;
        default:
            return false;
    }
}

bool ToPlayerShield(int value, PlayerShield& result) {
    switch (value) {
        case PLAYER_SHIELD_NONE:
            result = PlayerShield::None;
            return true;
        case PLAYER_SHIELD_HEROS_SHIELD:
            result = PlayerShield::Hero;
            return true;
        case PLAYER_SHIELD_MIRROR_SHIELD:
            result = PlayerShield::Mirror;
            return true;
        default:
            return false;
    }
}

} // namespace
} // namespace Zelda3D::MM3D

extern "C" unsigned long long Zelda3D_MM_PlayerSheathMeshMask(int playerForm, int sheathType, int currentShield,
                                                              int currentMask, int swordEquipValue) {
    using namespace Zelda3D::MM3D;
    PlayerSheathState state{};
    if (!PlayerModelFormFromRetailIndex(playerForm, state.form) || !ToPlayerSheathType(sheathType, state.sheathType) ||
        !ToPlayerShield(currentShield, state.shield) || !PlayerSwordFromRetailIndex(swordEquipValue, state.sword)) {
        return 0;
    }
    state.giantMask = currentMask == PLAYER_MASK_GIANT;
    return PlayerSheathMeshMaskForState(state);
}
