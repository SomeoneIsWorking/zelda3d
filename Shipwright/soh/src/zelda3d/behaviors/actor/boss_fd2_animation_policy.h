#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_ANIMATION_POLICY_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_ANIMATION_POLICY_H

namespace Zelda3D::BossFd2Animation {

enum class Action {
    Unknown,
    Wait,
    Emerge,
    Idle,
    BreatheFire,
    ClawSwipe,
    Burrow,
    Vulnerable,
    Damaged,
    Death,
};

struct InitialSelection {
    const char* csab = nullptr;
    bool crossfade = false;
};

// Exact OoT3D slot selected when a persistent hole-form action begins. Unknown actions deliberately
// have no selection: callers must retain the native draw instead of inventing an idle fallback.
InitialSelection InitialSelectionForAction(Action action);

} // namespace Zelda3D::BossFd2Animation

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_ANIMATION_POLICY_H
