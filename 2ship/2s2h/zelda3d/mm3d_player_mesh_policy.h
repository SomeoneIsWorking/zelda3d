#pragma once

#include <cstdint>

#include "mm3d_player_model_policy.h"

namespace Zelda3D::MM3D {

// MM3D Player_Draw's base visibility reset. Equipment/hand selectors run after
// this in retail and are intentionally outside this policy until recovered.
std::uint64_t PlayerBaseMeshMaskForForm(PlayerModelForm form);

} // namespace Zelda3D::MM3D
