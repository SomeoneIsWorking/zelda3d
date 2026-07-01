// mm3d_draw — MM's actor draw-divert seam (the MM analog of OoT's SoH3D_TryDrawActor).
// Plain C: called from the MM decomp draw path in z_actor.c. Decides, per actor, whether
// a registered MM3D CMB replaces the N64 model; if so it emits the model draw into the
// OPA display list (via the shared G_SOH3D_DRAW opcode) and returns 1 so the caller skips
// the vanilla N64 draw. Returns 0 to draw vanilla.
#pragma once

struct PlayState;
struct Actor;

#ifdef __cplusplus
extern "C" {
#endif

// 1 = drew the MM3D replacement (skip N64 draw); 0 = no replacement (draw vanilla N64).
int MM3D_TryDrawActor(struct PlayState* play, struct Actor* actor);

#ifdef __cplusplus
}
#endif
