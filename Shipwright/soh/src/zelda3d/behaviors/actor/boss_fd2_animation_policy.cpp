#include "boss_fd2_animation_policy.h"

namespace Zelda3D::BossFd2Animation {

InitialSelection InitialSelectionForAction(Action action) {
    switch (action) {
        case Action::Wait:
            return { "vba_wait", false };
        case Action::Emerge:
            return { "vba_up", false };
        case Action::Idle:
            return { "vba_search", false };
        case Action::BreatheFire:
            return { "vba_atack", true };
        case Action::ClawSwipe:
            return { "vba_tyokkai", true };
        case Action::Burrow:
            return { "vba_down", true };
        case Action::Vulnerable:
            return { "vba_hit", true };
        case Action::Damaged:
            return { "vba_beforedamage", true };
        case Action::Death:
            return { "vba_damage", true };
        case Action::Unknown:
            return {};
    }
    return {};
}

} // namespace Zelda3D::BossFd2Animation
