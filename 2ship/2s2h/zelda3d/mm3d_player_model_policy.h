#pragma once

namespace Zelda3D::MM3D {

enum class PlayerModelForm {
    FierceDeity,
    Goron,
    Zora,
    Deku,
    Human,
};

struct PlayerModelAsset {
    const char* garPath;
    const char* cmbName;
};

// Convert MM3D's five-value retail form index to the policy enum.
bool PlayerModelFormFromRetailIndex(int formIndex, PlayerModelForm& result);

// Retail MM3D stores each transformation's body in a dedicated *_new actor archive.
// This policy owns that asset identity; the Player adapter owns conversion from 2S2H enums.
const PlayerModelAsset& PlayerModelAssetForForm(PlayerModelForm form);

} // namespace Zelda3D::MM3D
