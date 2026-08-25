// Boss_Fd diagnostic death-state control.
#include "forced_death.h"
#include "history_layout.h"

#include "overlays/actors/ovl_Boss_Fd/z_boss_fd.h"

extern "C" int Zelda3D_BossFdForceDeath(Actor* actor, int liveSegments, int actionState) {
    if (!actor || actor->id != ACTOR_BOSS_FD || liveSegments < 0 ||
        liveSegments > Zelda3D::BossFdHistoryLayout::kBodySegmentCount || actionState < BOSSFD_DEATH_START ||
        actionState > BOSSFD_SKULL_BURN) {
        return 0;
    }
    BossFd* boss = reinterpret_cast<BossFd*>(actor);
    boss->work[BFD_ACTION_STATE] = actionState;
    boss->skinSegments = liveSegments;
    boss->timers[0] = 30000;
    boss->timers[1] = 30000;
    for (s16& state : boss->bodyFallApart)
        state = 0;
    return 1;
}
