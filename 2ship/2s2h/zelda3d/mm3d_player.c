// mm3d_player — see mm3d_player.h. Stage 1 stub: establishes the dedicated Player
// hook seam without changing behavior. Off by default; opt-in with MM_ZELDA3D_LINK=1.
//
// When enabled, the stub delegates to the generic Zelda3D_TryDrawActor — so if some
// model provider ever registers a substitute for ACTOR_PLAYER, that path handles it.
// This is deliberately trivial. Stage 2 replaces it with a real MmPlayerBehavior:
// per-form CMB selection (link_boy for HUMAN, link_goron/zora/nuts for the others),
// mesh-mask policy from player->leftHandType/rightHandType, and OoT3D bind retarget.
//
// Kept in plain C because z_player.c calls it — no C++ struct exposure across the seam.
#include "2s2h/zelda3d/mm3d_player.h"
#include "2s2h/zelda3d/mm3d_draw.h" // Zelda3D_TryDrawActor
#include <stdlib.h> // getenv

static int mm_link_enabled(void) {
    // Snapshot once: env-var reads are dozens of ns each; the Player draw runs per-frame.
    // Any change requires a restart — matches the OoT ZELDA3D_LINK convention.
    static int sCached = -1;
    if (sCached < 0) {
        const char* v = getenv("MM_ZELDA3D_LINK");
        sCached = (v != NULL && v[0] != '\0' && v[0] != '0') ? 1 : 0;
    }
    return sCached;
}

int Zelda3D_TryDrawPlayer(PlayState* play, Actor* actor) {
    if (!mm_link_enabled()) return 0;
    if (play == NULL || actor == NULL) return 0;
    // Stage 1: no dedicated player draw yet — just route through the generic actor
    // divert so an MM3D CMB registered for ACTOR_PLAYER (0x000) still gets picked up.
    // Stage 2 replaces this call with MmPlayerBehavior::instance().tryDrawModel(...).
    return Zelda3D_TryDrawActor(play, actor);
}
