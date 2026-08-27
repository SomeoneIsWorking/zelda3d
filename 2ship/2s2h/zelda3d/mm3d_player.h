// mm3d_player — MM's dedicated Link-render seam. The hook selects the retail MM3D body
// archive for the live transformation and defers its skinned replacement to Player_DrawImpl's
// SkelAnime_DrawFlexLod call. Base, sheath, right-hand visibility, and shared animation have
// focused owners; left-hand visibility remains incomplete, so MM_ZELDA3D_LINK stays opt-in.
#pragma once
#include "global.h" // PlayState, Actor

#ifdef __cplusplus
extern "C" {
#endif

// Currently returns 0 so Player_DrawGameplay supplies its live skeleton and post-limb callbacks.
// With a resolved form model, the SkelAnime seam replaces that draw; otherwise it stays vanilla.
int Zelda3D_TryDrawPlayer(PlayState* play, Actor* actor);

#ifdef __cplusplus
}
#endif
