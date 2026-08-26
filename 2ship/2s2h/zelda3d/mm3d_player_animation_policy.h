#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "mm3d_player_model_policy.h"

namespace Zelda3D::MM3D {

struct PlayerAnimationPathCandidates {
    std::array<std::string, 2> paths;
    std::size_t count = 0;
};

// Convert a named gameplay_keep PlayerAnimation resource into exact members of the retail
// zelda2_link_new archive. Candidate order is authoritative: Human-specific child clips win,
// then Human may reuse the shared boy corpus; every other form stays in its own directory.
PlayerAnimationPathCandidates PlayerAnimationPathsForForm(PlayerModelForm form, const char* animationOtr);

} // namespace Zelda3D::MM3D
