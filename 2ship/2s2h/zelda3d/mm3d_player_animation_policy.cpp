#include "mm3d_player_animation_policy.h"

#include <string_view>

namespace Zelda3D::MM3D {
namespace {

constexpr std::string_view kOtrPrefix = "__OTR__";
constexpr std::string_view kPlayerAnimationPrefix = "objects/gameplay_keep/gPlayerAnim_";

void AddCandidate(PlayerAnimationPathCandidates& candidates, std::string_view directory, std::string_view clip) {
    candidates.paths[candidates.count++] = std::string(directory) + "/anim/" + std::string(clip) + ".csab";
}

} // namespace

PlayerAnimationPathCandidates PlayerAnimationPathsForForm(PlayerModelForm form, const char* animationOtr) {
    PlayerAnimationPathCandidates candidates;
    if (animationOtr == nullptr) {
        return candidates;
    }

    std::string_view resource(animationOtr);
    if (resource.starts_with(kOtrPrefix)) {
        resource.remove_prefix(kOtrPrefix.size());
    }
    if (!resource.starts_with(kPlayerAnimationPrefix)) {
        return candidates;
    }
    resource.remove_prefix(kPlayerAnimationPrefix.size());
    if (resource.empty() || resource.find('/') != std::string_view::npos) {
        return candidates;
    }

    switch (form) {
        case PlayerModelForm::FierceDeity:
            AddCandidate(candidates, "boy", resource);
            break;
        case PlayerModelForm::Goron:
            AddCandidate(candidates, "goron", resource);
            break;
        case PlayerModelForm::Zora:
            AddCandidate(candidates, "zora", resource);
            break;
        case PlayerModelForm::Deku:
            AddCandidate(candidates, "nuts", resource);
            break;
        case PlayerModelForm::Human:
            AddCandidate(candidates, "child", resource);
            AddCandidate(candidates, "boy", resource);
            break;
    }
    return candidates;
}

} // namespace Zelda3D::MM3D
