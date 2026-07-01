#pragma once

#include <cstdint>
#include "fast/soh3d_sg_ubo.h" // SoH3DSg::kCommonBytes/kBonesBytes, SOH3D_GL_MAX_BONES

// Render-unification effort (kanban #131), Phase 2. CPU-side mirror of the combined UBO declared
// in unified_shader.cpp's kCommonUboBody — MUST stay byte-identical (same field order/sizes; every
// field is vec4/mat4/ivec4-sized so std140 offsets equal C offsets, same discipline as SgUbo in
// soh3d_sg_ubo.h). Deliberately sized to fit SoH3DSg::kCommonBytes (320 bytes) exactly so a unified
// draw reuses the EXISTING DRAW_MODEL Op / AppendSoH3DModelDraw / mSoh3dModelUbos plumbing
// (gfx_sdl3gpu.h/.cpp) unchanged — see unified_shader.cpp's kCommonUboBody comment.
namespace SoH3DUnified {

struct CommonUbo {
    float uMvp[16];
    float uMv[16];
    float uLightDir[4];
    int32_t uCombA[16]; // combMux[2][2][4] flattened
    float uPrimColor[4];
    float uEnvColor[4];
    float uFogColor[4];
    float uParams0[4]; // x=alphaRef, y=lightingMode, z=cycleCount, w=frame_count
    float uParams1[4]; // x=noise_scale, y=polygonOffset, z=hasSkin, w unused
    float uMatAmbient[4];
    float uMatDiffuse[4];
};

static_assert(sizeof(CommonUbo) == SoH3DSg::kCommonBytes,
              "CommonUbo must byte-match unified_shader.cpp's kCommonUboBody AND SoH3DSg::kCommonBytes "
              "(320) — the whole point is reusing the existing DRAW_MODEL push path unchanged");

// Combined blob layout reused verbatim from SgUbo: kCommonBytes of CommonUbo, then kBonesBytes of
// bone matrices (SOH3D_GL_MAX_BONES mat4s) — the SAME two-block push AppendSoH3DModelDraw already
// does for the old CMB shader, just with different common-block contents.
struct UnifiedDrawUbo {
    CommonUbo common;
    float bones[SOH3D_GL_MAX_BONES * 16];
};

static_assert(sizeof(UnifiedDrawUbo) == sizeof(SoH3DSg::SgUbo),
              "UnifiedDrawUbo must be the same total size as SgUbo (common+bones) so it fits "
              "mSoh3dModelUbos's fixed-size storage (std::array<uint8_t, sizeof(SgUbo)>)");

} // namespace SoH3DUnified
