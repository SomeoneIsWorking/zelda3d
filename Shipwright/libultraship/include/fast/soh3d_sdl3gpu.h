// SoH3D SDL3 GPU render path — the SDL3-GPU counterpart of soh3d_vk.cpp's GPU work, built on the
// UNIFIED op model (user directive 2026-06-26, memory soh3d-unified-renderer-one-pass).
//
// Unlike the Vulkan path (which borrowed the backend's live command buffer mid-frame), this module
// records every OoT3D model draw as a first-class OP_DRAW appended into the SAME deferred op-list the
// SDL3 GPU backend replays in ONE render pass in FinishRender (AppendSoH3DModelDraw). So the 3DS
// content interleaves depth-correctly with the N64 triangles with no separate-pass handshake —
// one renderer, one pass, one bind path, no N64-vs-3DS distinction in how draws are submitted.
//
// The backend-agnostic bookkeeping (draw list, per-emit pose capture, light/shadow/AO tunables, the
// SoH3D_GL_* C-ABI) stays in soh3d_gl.cpp; when the live Fast3D backend is the SDL3 GPU one those
// entry points dispatch the GPU submission here. This module owns its own SDL3 GPU resources
// (per-model vertex buffers + textures, the model pipeline cache, per-draw uniforms).
#pragma once
#include "fast/soh3d_gl.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 1 if the live Fast3D backend is the SDL3 GPU one (so the GL entry points dispatch here).
int SoH3D_Sg_Active(void);

// Mirror of SoH3D_GL_SetModelProvider for the SDL3 GPU model store.
void SoH3D_Sg_SetProvider(SoH3DModelProvider fn);

// Bracket the SoH3D SDL3 GPU submission. BeginPass resolves resources + starts the per-frame model
// pass; DrawModel appends one (already pose-resolved) model as an in-pass op; EndPass finalizes.
// The caller (SoH3D_GL_RenderPass) does the per-item pose interpolation, exactly as for GL/Vulkan.
void SoH3D_Sg_BeginPass(void);
// matTex: a const std::unordered_map<int,int>* (material->texIndex facial override), passed as void*.
void SoH3D_Sg_DrawModel(int modelId, const float* mp16, const float* mv16, int lit, int invertY,
                        unsigned char r, unsigned char g, unsigned char b, unsigned char a, float aspectAdj,
                        const float* boneData, int boneCnt, unsigned long long midMask, int sky,
                        float uvOffU, float uvOffV, const void* matTex);
void SoH3D_Sg_EndPass(void);

// Mirror of SoH3D_GL_RequestEvictRange for the SDL3 GPU model store.
void SoH3D_Sg_RequestEvictRange(int lo, int hi);

// --- Dynamic sun-shadows + screen-space AO (offscreen passes; appended as own-pass ops) ---
// Same dispatch shape as the Vulkan path. Return 0 / no-op until the effect is enabled.
int SoH3D_Sg_BeginShadowPass(void);
void SoH3D_Sg_ShadowCasterDraw(int modelId, const float* mp16, const float* mv16, const float* boneData,
                               int boneCnt, unsigned long long midMask);
void SoH3D_Sg_ShadowCasterTris(const float* worldXYZ, size_t triCount, const float* lightVP16);
void SoH3D_Sg_EndShadowPass(void);
void SoH3D_Sg_SetShadow(int on, const float* lightVP16);
int SoH3D_Sg_BeginDepthPrepass(void);
void SoH3D_Sg_DepthPrepassDraw(int modelId, const float* mp16, const float* mv16, int invertY, float aspectAdj,
                               const float* boneData, int boneCnt, unsigned long long midMask, int sky);
void SoH3D_Sg_EndDepthPrepass(void);
void SoH3D_Sg_AoComposite(void);

#ifdef __cplusplus
}
#endif
