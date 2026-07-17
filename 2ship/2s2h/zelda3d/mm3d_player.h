// mm3d_player — MM's dedicated Link-render seam (Stage 1 of the MM/OoT Link unification,
// see scratch/plans/mm_oot_link_unify.md). Mirrors OoT's Zelda3D_TryDrawPlayer surface.
//
// z_player.c's primary back-cull Player_DrawGameplay call guards on this: when the hook
// returns 1, the vanilla N64 draw is skipped and the MM3D replacement was emitted
// instead. Returns 0 today unless MM_ZELDA3D_LINK is set, in which case it delegates to
// the generic actor path (Zelda3D_TryDrawActor) — no per-form policy yet. Stage 2 grows
// the MmPlayerBehavior subclass on top of this seam.
#pragma once
#include "global.h" // PlayState, Actor

#ifdef __cplusplus
extern "C" {
#endif

// 1 = MM3D replacement drew, skip vanilla N64. 0 = fall through to Player_DrawGameplay.
int Zelda3D_TryDrawPlayer(PlayState* play, Actor* actor);

#ifdef __cplusplus
}
#endif
