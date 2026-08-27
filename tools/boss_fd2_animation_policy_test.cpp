#include "boss_fd2_animation_policy.h"

#include <array>
#include <iostream>
#include <string_view>

using Zelda3D::BossFd2Animation::Action;
using Zelda3D::BossFd2Animation::InitialSelectionForAction;

int main() {
    struct ExpectedSelection {
        Action action;
        std::string_view csab;
        bool crossfade;
    };
    constexpr std::array kExpected = {
        ExpectedSelection{ Action::Wait, "vba_wait", false },
        ExpectedSelection{ Action::Emerge, "vba_up", false },
        ExpectedSelection{ Action::Idle, "vba_search", false },
        ExpectedSelection{ Action::BreatheFire, "vba_atack", true },
        ExpectedSelection{ Action::ClawSwipe, "vba_tyokkai", true },
        ExpectedSelection{ Action::Burrow, "vba_down", true },
        ExpectedSelection{ Action::Vulnerable, "vba_hit", true },
        ExpectedSelection{ Action::Damaged, "vba_beforedamage", true },
        ExpectedSelection{ Action::Death, "vba_damage", true },
    };
    for (const ExpectedSelection& expected : kExpected) {
        const auto selection = InitialSelectionForAction(expected.action);
        if (selection.csab == nullptr || std::string_view(selection.csab) != expected.csab ||
            selection.crossfade != expected.crossfade) {
            std::cerr << "incorrect BossFd2 initial selection\n";
            return 1;
        }
    }
    const auto unknown = InitialSelectionForAction(Action::Unknown);
    if (unknown.csab != nullptr || unknown.crossfade) {
        std::cerr << "unknown BossFd2 action selected a 3DS animation\n";
        return 1;
    }
    return 0;
}
