#pragma once

#include <cstdint>
#include "fast/zelda3d_sg_ubo.h" // Zelda3DSg::kCommonBytes/kBonesBytes, ZELDA3D_GL_MAX_BONES

// Render-unification effort (kanban #131), Phase 2. CPU-side mirror of the combined UBO declared
// in unified_shader.cpp's kCommonUboBody — MUST stay byte-identical (same field order/sizes; every
// field is vec4/mat4/ivec4-sized so std140 offsets equal C offsets, same discipline as SgUbo in
// zelda3d_sg_ubo.h). Deliberately sized to fit Zelda3DSg::kCommonBytes (368 bytes, since the
// uSheen + uTex1Xf fields were added) exactly so a unified draw reuses the EXISTING
// DRAW_MODEL Op / AppendZelda3DModelDraw / mSoh3dModelUbos plumbing (gfx_sdl3gpu.h/.cpp) unchanged
// — see unified_shader.cpp's kCommonUboBody comment.
namespace Zelda3DUnified {

struct CommonUbo {
    float uMvp[16];
    float uMv[16];
    float uLightDir[4];
    int32_t uCombA[16]; // combMux[2][2][4] flattened
    float uPrimColor[4];
    float uEnvColor[4];
    float uFogColor[4];
    float uParams0[4]; // x=alphaRef, y=lightingMode, z=cycleCount, w=frame_count
    float uParams1[4]; // x=noise_scale, y=polygonOffset, z=hasSkin, w=alreadyTransformed (N64)
    float uMatAmbient[4];
    float uMatDiffuse[4];
    // Mirror of SgUbo::uMatConst so sizeof(UnifiedDrawUbo) stays == sizeof(SgUbo); the unified
    // shader currently ignores it, but the size parity is enforced by static_assert and is what
    // lets the unified path reuse the existing DRAW_MODEL push-block plumbing unchanged.
    float uMatConst[4];
    // Mirror of SgUbo::uSheen (title wordmark sheen, title_logo_actor.md §6.3). The unified
    // renderer (gUnifiedRenderer, off by default) doesn't draw the title overlay today — this is
    // pure size-parity padding, same rationale as uMatConst above.
    float uSheen[4];
    // Mirror of SgUbo::uTex1Xf (dual-texture coordinator-1 transform, fire-glow). Size-parity
    // padding for the unified path, same rationale as uMatConst/uSheen above.
    float uTex1Xf[4];
};

static_assert(sizeof(CommonUbo) == Zelda3DSg::kCommonBytes,
              "CommonUbo must byte-match unified_shader.cpp's kCommonUboBody AND Zelda3DSg::kCommonBytes "
              "(368) — the whole point is reusing the existing DRAW_MODEL push path unchanged");

// Combined blob layout reused verbatim from SgUbo: kCommonBytes of CommonUbo, then kBonesBytes of
// bone matrices (ZELDA3D_GL_MAX_BONES mat4s) — the SAME two-block push AppendZelda3DModelDraw already
// does for the old CMB shader, just with different common-block contents.
struct UnifiedDrawUbo {
    CommonUbo common;
    float bones[ZELDA3D_GL_MAX_BONES * 16];
};

static_assert(sizeof(UnifiedDrawUbo) == sizeof(Zelda3DSg::SgUbo),
              "UnifiedDrawUbo must be the same total size as SgUbo (common+bones) so it fits "
              "mSoh3dModelUbos's fixed-size storage (std::array<uint8_t, sizeof(SgUbo)>)");

} // namespace Zelda3DUnified
