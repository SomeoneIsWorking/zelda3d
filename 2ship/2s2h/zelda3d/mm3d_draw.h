// mm3d_draw — MM's actor draw-divert seam (the MM analog of OoT's Zelda3D_TryDrawActor).
// Plain C: called from the MM decomp draw path in z_actor.c. Decides, per actor, whether
// a registered MM3D CMB replaces the N64 model; if so it emits the model draw into the
// OPA display list (via the shared G_ZELDA3D_DRAW opcode) and returns 1 so the caller skips
// the vanilla N64 draw. Returns 0 to draw vanilla.
#pragma once
#include "global.h"

// NB: MM's Room is a typedef of an anonymous struct (z64scene.h), so we can't forward-declare
// it as `struct Room`. Callers of this header (z_room.c / z_play.c / mm3d_draw.c) all include
// global.h first, which pulls in the real PlayState/Actor/Room typedefs — so we just use those
// names here directly. Do NOT forward-declare `struct Room` — it would create a distinct type.

#ifdef __cplusplus
extern "C" {
#endif

// SkelAnime intercept — called at the top of MM's SkelAnime_Draw*Opa entry points.
// If a skinned MM3D replacement is pending for this actor, poses OoT3D bones from
// the live N64 jointTable and returns 1 (caller returns immediately). Else 0
// (caller proceeds with the vanilla N64 limb walk).
int Zelda3D_MM_InterceptSkelAnime(PlayState* play, Actor* actor, void** skeleton, Vec3s* jointTable);

// #107: after Zelda3D_MM_InterceptSkelAnime returns 1 (MM3D model drawn instead of the N64
// limb walk), each SkelAnime_Draw*Opa call site re-invokes the same walk with
// gZelda3dMmColliderPass=1 so the vanilla N64 postLimbDraw side effects still run —
// most importantly Collider_UpdateSpheres, which keeps a replaced actor's OC/AC/AT
// collision spheres pinned to its live limbs instead of stuck at the origin (else
// enemies phantom-collide and zip away, exactly like OoT's own #107). The re-walked
// gfx output is discarded by the caller rewinding polyOpa/polyXlu — this flag ONLY
// suppresses the replacement so the walk proceeds. Mirrors OoT's gZelda3dColliderPass.
extern int gZelda3dMmColliderPass;

// 1 = drew the MM3D replacement (skip N64 draw); 0 = no replacement (draw vanilla N64).
// This is the only MM-specific renderer surface: the per-actor divert (MM has its own
// actor/object tables). The model op it emits (G_ZELDA3D_DRAW) is appended INLINE into the one
// unified render pass alongside the N64 geometry — the same shared renderer OoT uses. There is
// ONE renderer and ONE pass; no per-game copy and no separate Zelda3D render-pass drain.
int Zelda3D_TryDrawActor(PlayState* play, Actor* actor);

// Emit one resolved model using the MM decomp display-list/matrix macros.
void Zelda3D_MM_EmitModelDraw(void* play, void* actor, int modelId, float worldScale, float groundOffset);

// Per-room scene divert (MM analog of the OoT implementation). Returns 1 when the
// current MM scene has an MM3D room CMB registered and this call drew it — caller
// then skips the N64 room mesh. Currently returns 0 unconditionally (no MM3D scene
// coverage table yet); the guard is wired now so no z_room.c edit is needed the
// day the first mapping lands. #134-style.
int Zelda3D_TryDrawRoom(PlayState* play, Room* room);

// MM3D scene folder name for this scene ("z2_clocktower"), or NULL when the scene has no MM3D
// counterpart. Shared with the collision path so the sceneNum->name table has ONE owner.
const char* Zelda3D_MM_SceneName(PlayState* play);

// Predicate mirror of Zelda3D_TryDrawRoom used to suppress the N64 pre-rendered
// background image (skybox path in Play_Draw): returns 1 when MM3D is going to
// cover this scene — the same rule OoT applies. Under the game-wide "no N64
// renderer under Zelda3D" invariant this returns 1 whenever Zelda3D is on, so
// an unmapped MM scene renders as an empty room instead of a 2D bg image.
int Zelda3D_ShouldSuppressBgImageSkybox(PlayState* play);

#ifdef __cplusplus
}
#endif
