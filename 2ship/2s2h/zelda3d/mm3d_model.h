// mm3d_model — MM's per-game model-substitution provider (the MM analog of OoT's
// zelda3d_model.cpp). Owns the MM3D asset ROM (CtrRom over ZELDA3D_MM3D_ROM), the
// actor/object -> CMB tables, and the renderer model provider registered through the
// SHARED libultraship seam Zelda3D_GL_SetModelProvider. Geometry conversion reuses the
// shared cmb3d converter (Zelda3D::MakeGlGroup / AppendCmbTextures) — no duplication.
//
// C-linkage so the MM decomp draw path (mm3d_draw.c, plain C) can call it.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Register the renderer's model provider once (idempotent). Safe to call every frame.
void Zelda3D_EnsureModelProvider(void);

// Resolve an MM3D replacement for a live actor. Given the actor id and its loaded
// N64 object id, returns 1 and fills *modelId (the renderer handle to draw),
// *worldScale (N64 world units per CMB unit) and *groundOffset (model-space Y lift
// onto the actor's ground) when a CMB is registered; returns 0 (draw vanilla N64)
// otherwise. Currently always 0 until the MM3D archive tables are populated.
int Zelda3D_LookupModel(int actorId, int objectId, int* modelId, float* worldScale, float* groundOffset);

// World scale for a resolved model id (1.0 if unknown). Kept parallel to the OoT API.
float Zelda3D_ModelScaleById(int modelId);

// True (1) if the resolved model's CMB is skinned (>1 bone) — the caller should
// defer to the SkelAnime intercept instead of emitting a static draw.
int Zelda3D_IsModelSkinned(int modelId);

// Live per-object world-scale override for the prop under calibration
// (REPL `mscale <objId> <scale>`). Persisted on the model's ModelSpec so it
// survives across draws. If the object hasn't been auto-probed yet, the scale
// is stashed and applied when the archive is first resolved. Passing scale <=0
// clears any override for that object (falls back to the auto-probe default).
void Zelda3D_SetObjectScale(int objectId, float scale);

// Dump the current object->model->scale table via the supplied line sink
// (one line per entry). Used by the REPL `mlist` command.
void Zelda3D_ListModels(void (*emitLine)(const char* line, void* user), void* user);

// STAGE 2 SkelAnime port surface. See docs/MM_SKELANIME_PORT.md.
//
// Zelda3D_MM_SetPending — called from mm3d_draw.c::Zelda3D_TryDrawActor when a
// skinned model is resolved. Defers the draw: instead of emitting immediately,
// stash actor + modelId + scale + groundOffset so the SkelAnime intercept can
// pose the OoT3D bones from the live N64 jointTable.
void Zelda3D_MM_SetPending(void* actor, int modelId, float worldScale, float groundOffset);

// Zelda3D_MM_SkelAnimeDrawRaw — SkelAnime intercept. Called from the top of MM's
// SkelAnime_DrawOpa / DrawFlexOpa / DrawLod / DrawFlexLod. If a pending model is
// set, retargets N64 jointTable rotations onto the OoT3D CMB bones (identity
// bone->limb correspondence), uploads the skin matrices to the renderer, emits
// the OoT3D draw, and returns 1 (SkelAnime skips its N64 limb walk). Returns 0
// to fall through to N64.
struct PlayState;
int Zelda3D_MM_SkelAnimeDrawRaw(struct PlayState* play, void** skeleton, void* jointTable /* Vec3s* */,
                                int limbCount);

// Zelda3D_MM_AfterActorDraw — clears pending state at the end of an actor's Draw
// so the next actor starts fresh. Called from a post-draw hook in mm3d_draw.c.
void Zelda3D_MM_AfterActorDraw(void);

// STAGE 3 auto-scale + ground-offset. Mirrors OoT's Zelda3D_AutoModelBoneLenSum
// and Zelda3D_AutoModelMinY. The OoT3D CMB and the live N64 actor are the SAME
// character (Grezzo port), so the rest-pose bone-length ratio
//   worldScale = actor->scale.x * (n64BoneSum / cmbBoneSum)
// is the correct OoT3D->world scale — pose-independent and free of the bbox
// overshoot that made the default 0.1f-scale skinned actors render tiny.
//
// Zelda3D_MM_ModelBoneLenSum — Σ|CMB bone trans| for non-root bones. 0 if unloaded.
// Zelda3D_MM_ModelMinY       — bind-pose local-space minimum vertex Y (feet lift).
// Zelda3D_MM_OverridePending — replace the pending scale/groundOffset with the
//                              auto-derived values. Called from the SkelAnime
//                              intercept after the live N64 skeleton sum is known.
float Zelda3D_MM_ModelBoneLenSum(int modelId);
float Zelda3D_MM_ModelMinY(int modelId);
void  Zelda3D_MM_OverridePending(float worldScale, float groundOffset);
int   Zelda3D_MM_PendingModelId(void); // -1 if no pending replacement.

// Zelda3D_MM_GradeTopology — deterministic grade of the identity bone->limb retarget
// (mmUpdateAnimN64's `limb = bone.id` assumption). Compares the live N64 skeleton's
// per-limb PARENT array (caller reconstructs it from the child/sibling tree) against
// the loaded CMB bone hierarchy, index-for-index. Where the parent arrays match, the
// identity map is provably correct; where they diverge, that archive needs a per-archive
// BoneMap. Env-gated by ZELDA3D_MM_SKINNED_TOPO=1, logs [MM3D-TOPO] once per modelId.
void  Zelda3D_MM_GradeTopology(int modelId, const int* n64Parents, int n64LimbCount);

#ifdef __cplusplus
}
#endif
