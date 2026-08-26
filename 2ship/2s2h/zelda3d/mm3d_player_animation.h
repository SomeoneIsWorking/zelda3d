#pragma once

#include "asset/gar.h"
#include "mm3d_player_model_policy.h"

namespace Zelda3D::MM3D {

void RegisterPlayerAnimationModel(int modelId, PlayerModelForm form);
bool IsPlayerAnimationModel(int modelId);

// Returns a stable pointer to the exact GAR member path for the live N64 PlayerAnimation, or null
// when the named clip is absent. No basename lookup or unlisted cross-form fallback is performed.
const char* ResolvePlayerAnimationPath(int modelId, const char* animationOtr);

// The shared retail animation bank for a registered Player model. Ownership stays in this module.
const Gar* PlayerAnimationArchive(int modelId);

} // namespace Zelda3D::MM3D
