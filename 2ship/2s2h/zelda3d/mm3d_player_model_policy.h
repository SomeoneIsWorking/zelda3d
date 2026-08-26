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

// Retail MM3D stores each transformation's body in a dedicated *_new actor archive.
// This policy owns that asset identity; the Player adapter owns conversion from 2S2H enums.
const PlayerModelAsset& PlayerModelAssetForForm(PlayerModelForm form);

} // namespace Zelda3D::MM3D
