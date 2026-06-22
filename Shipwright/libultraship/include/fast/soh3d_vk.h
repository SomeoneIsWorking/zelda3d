// SoH3D Vulkan render pass — the Vulkan counterpart of soh3d_gl.cpp's GPU work.
//
// The backend-agnostic bookkeeping (the draw list, per-emit pose capture, the light/shadow/AO
// tunables, and the C-ABI entry points SoH3D_GL_*) stays in soh3d_gl.cpp; when the live Fast3D
// backend is the Vulkan one, those entry points dispatch the actual GPU submission here. This
// module owns its own Vulkan resources (per-model vertex buffers + textures, the model pipeline,
// per-draw uniform/bone ring) and records its draws into the backend's current command buffer and
// render pass (GfxRenderingAPIVulkan::BeginSoH3DPass), so the OoT3D content interleaves
// depth-correctly with the N64 geometry.
//
// NOTE: screen-space AO IS ported here (an offscreen depth pre-pass + an in-pass SSAO composite,
// see SoH3D_Vk_BeginDepthPrepass/AoComposite). Dynamic sun-shadows (the GL pass's other offscreen
// pass) are still GL-only — a follow-up. Build-structure NOTE: the C-ABI entry points currently live in soh3d_gl.cpp (compiled
// only with ENABLE_OPENGL); a no-GL build (macOS) will need that bookkeeping extracted to a shared
// TU. See the dispatch block in soh3d_gl.cpp.
#pragma once
#include "fast/soh3d_gl.h"
#include <stddef.h> // size_t (SoH3D_Vk_ShadowCasterTris)

#ifdef __cplusplus
extern "C" {
#endif

// 1 if the live Fast3D backend is the Vulkan one (so the GL entry points should dispatch here).
int SoH3D_Vk_Active(void);

// Mirror of SoH3D_GL_SetModelProvider for the Vulkan model store (the GL setter forwards to this).
void SoH3D_Vk_SetProvider(SoH3DModelProvider fn);

// Bracket the SoH3D Vulkan pass: BeginPass opens the backend's current render pass; DrawModel
// records one (already pose-resolved) model; EndPass is a no-op placeholder for symmetry. The
// caller (SoH3D_GL_RenderPass) does the per-item pose interpolation, exactly as for the GL path.
void SoH3D_Vk_BeginPass(void);
// matTex: a const std::unordered_map<int,int>* (material->texIndex facial override), passed as void*
// to keep the header C-linkage clean; null = no override. Mirrors the GL drawOne matTex channel.
void SoH3D_Vk_DrawModel(int modelId, const float* mp16, const float* mv16, int lit, int invertY,
                        unsigned char r, unsigned char g, unsigned char b, unsigned char a, float aspectAdj,
                        const float* boneData, int boneCnt, unsigned long long midMask, int sky,
                        float uvOffU, float uvOffV, const void* matTex);
void SoH3D_Vk_EndPass(void);

// --- Screen-space ambient occlusion (the Vulkan counterpart of soh3d_gl.cpp's aoPass) ---
// A render pass cannot be nested inside the main FB pass, so AO is recorded as a SEPARATE
// offscreen render pass BEFORE the main SoH3D draws, then composited as a full-screen multiply
// INSIDE the main pass after them. The dispatcher (SoH3D_GL_RenderPass) drives this sequence:
//   if (BeginDepthPrepass()) { for each non-sky item DepthPrepassDraw(...); EndDepthPrepass(); }
//   BeginPass(); for each item DrawModel(...); AoComposite(); EndPass();
// BeginDepthPrepass returns 0 (skip the AO passes) when AO is off or resources are unavailable.
int SoH3D_Vk_BeginDepthPrepass(void);
void SoH3D_Vk_DepthPrepassDraw(int modelId, const float* mp16, const float* mv16, int invertY, float aspectAdj,
                               const float* boneData, int boneCnt, unsigned long long midMask, int sky);
void SoH3D_Vk_EndDepthPrepass(void);
void SoH3D_Vk_AoComposite(void);

// --- Dynamic sun-shadows (the Vulkan counterpart of soh3d_gl.cpp's renderShadowMap/shadowLit) ---
// The shadow map is rendered from the light's POV in its OWN offscreen render pass (before the AO
// pre-pass / main pass); the visible model draws then PCF-sample it. The dispatcher drives:
//   if (BeginShadowPass()) { for each lit non-sky caster ShadowCasterDraw(lightVP*mv,...);
//                            EndShadowPass(); SetShadow(1, lightVP); }
//   ... AO pre-pass ... BeginPass(); DrawModel(...) (samples the shadow map per SetShadow); ...
// BeginShadowPass returns 0 (skip shadows) when off / no focus / unavailable.
int SoH3D_Vk_BeginShadowPass(void);
void SoH3D_Vk_ShadowCasterDraw(int modelId, const float* mp16, const float* mv16, const float* boneData,
                               int boneCnt, unsigned long long midMask);
void SoH3D_Vk_EndShadowPass(void);
// #72: record N64 opaque world-space caster triangles into the open shadow pass (world-space soup).
void SoH3D_Vk_ShadowCasterTris(const float* worldXYZ, size_t triCount, const float* lightVP16);
void SoH3D_Vk_SetShadow(int on, const float* lightVP16);

// Mirror of SoH3D_GL_RequestEvictRange for the Vulkan model store (the GL request forwards here):
// drop cached uploads with id in [lo,hi) at the next BeginPass so they re-upload at the new size.
void SoH3D_Vk_RequestEvictRange(int lo, int hi);

#ifdef __cplusplus
}
#endif
