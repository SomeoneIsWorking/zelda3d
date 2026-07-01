// mm3d_model — MM's per-game model-substitution provider (the MM analog of OoT's
// zelda3d_model.cpp). Owns the MM3D asset ROM (CtrRom over ZELDA3D_MM_3DS_ROM), the
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
void MM3D_EnsureModelProvider(void);

// Resolve an MM3D replacement for a live actor. Given the actor id and its loaded
// N64 object id, returns 1 and fills *modelId (the renderer handle to draw),
// *worldScale (N64 world units per CMB unit) and *groundOffset (model-space Y lift
// onto the actor's ground) when a CMB is registered; returns 0 (draw vanilla N64)
// otherwise. Currently always 0 until the MM3D archive tables are populated.
int MM3D_LookupModel(int actorId, int objectId, int* modelId, float* worldScale, float* groundOffset);

// World scale for a resolved model id (1.0 if unknown). Kept parallel to the OoT API.
float MM3D_ModelScaleById(int modelId);

// Live world-scale override for the prop under calibration (REPL `mscale <f>`; <=0 clears
// back to the table value). Debug knob for dialing in the first props against the oracle.
void MM3D_SetScaleOverride(float scale);

#ifdef __cplusplus
}
#endif
