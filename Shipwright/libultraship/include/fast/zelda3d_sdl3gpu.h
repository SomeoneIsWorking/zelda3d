// Zelda3D SDL3 GPU render path — the SDL3-GPU counterpart of zelda3d_vk.cpp's GPU work, built on the
// UNIFIED op model (user directive 2026-06-26, memory zelda3d-unified-renderer-one-pass).
//
// Unlike the Vulkan path (which borrowed the backend's live command buffer mid-frame), this module
// records every OoT3D model draw as a first-class OP_DRAW appended into the SAME deferred op-list the
// SDL3 GPU backend replays in ONE render pass in FinishRender (AppendZelda3DModelDraw). So the 3DS
// content interleaves depth-correctly with the N64 triangles with no separate-pass handshake —
// one renderer, one pass, one bind path, no N64-vs-3DS distinction in how draws are submitted.
//
// The backend-agnostic pose, material, lighting, and submission owners expose the Zelda3D_GL_* C ABI;
// when the live Fast3D backend is the SDL3 GPU one those entry points dispatch GPU work here. This
// module owns its SDL3 GPU resources (per-model vertex buffers and textures, the model pipeline cache,
// and per-draw uniforms).
#pragma once
#include "fast/zelda3d_model_provider.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 1 if the live Fast3D backend is the SDL3 GPU one (so the GL entry points dispatch here).
int Zelda3D_Sg_Active(void);

// Mirror of Zelda3D_GL_SetModelProvider for the SDL3 GPU model store.
void Zelda3D_Sg_SetProvider(Zelda3DModelProvider fn);

// Bracket the Zelda3D SDL3 GPU submission. BeginPass resolves resources + starts the per-frame model
// pass; DrawModel appends one (already pose-resolved) model as an in-pass op; EndPass finalizes.
// The focused submission owner resolves and interpolates each pose before calling this backend.
void Zelda3D_Sg_BeginPass(void);
// matTex: a const std::unordered_map<int,int>* (material->texIndex facial override), passed as void*.
// matConst: a const std::unordered_map<int, MatConstOv>* keyed by material*6+constant-slot,
// passed as void*. Layout of the value: { int constIdx; float rgba[4]; }. NULL / empty = no override.
// lightDirOv: NULL = use the scene's global light dir (gZelda3dLightDirWorld), no extra sheen
// term. Non-NULL = float[3] object-space direction (title_gl.h's Zelda3D_GL_SetLightDirOverride)
// — transformed by this draw's own mv16 (mat3(uMV), matching the vertex shader's normal
// transform) and fed as this draw's uLightDir, PLUS enables the additive diffuse "sheen" boost
// (uSheen in zelda3d_sg_ubo.h) instead of the shared darkening half-Lambert term. See
// title_logo_actor.md §6.3 / title_logo.cpp for the ground truth and derivation.
// sphereNormalOv: NULL = sphere-mapped coordinates derive from mat3(uMV)*n. Non-NULL = float[9]
// row-major copy of the target CmbVShader's c4-c6 normal transform, used when host-native
// composition carries a different placement matrix in uMV (see zelda3d_sg_ubo.h uSphNrm*).
void Zelda3D_Sg_DrawModel(int modelId, const float* mp16, const float* mv16, int lit, int invertY, unsigned char r,
                          unsigned char g, unsigned char b, unsigned char a, float aspectAdj, const float* boneData,
                          int boneCnt, unsigned long long midMask, int sky, float uvOffU, float uvOffV,
                          const void* matTex, const void* matConst, const void* matUv, int forceUnlit,
                          const float* lightDirOv = nullptr, const float* sphereNormalOv = nullptr);
void Zelda3D_Sg_EndPass(void);

// Mirror of Zelda3D_GL_RequestEvictRange for the SDL3 GPU model store.
void Zelda3D_Sg_RequestEvictRange(int lo, int hi);

// #146 item B: reset the shared depth buffer to "far" via a fullscreen depth-only draw (color
// writes off) appended at THIS point in the op-list — no render-pass split. Called once by
// Zelda3D_Overlay2D_Begin (via gSPZelda3DClearDepth / Zelda3D_ClearOverlayDepth) so the title
// overlay's own models depth-test correctly against each other without inheriting stale depth
// from the already-composited 3D scene.
void Zelda3D_Sg_ClearOverlayDepth(void);

#ifdef __cplusplus
}
#endif
