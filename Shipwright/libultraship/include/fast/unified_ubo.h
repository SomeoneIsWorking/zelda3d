#pragma once

#include <cstdint>
#include "fast/zelda3d_sg_ubo.h" // Zelda3DSg::kCommonBytes/kBonesBytes, ZELDA3D_GL_MAX_BONES

// Render-unification effort (kanban #131), Phase 2. CPU-side mirror of the combined UBO declared
// in unified_shader.cpp's kCommonUboBody — MUST stay byte-identical (same field order/sizes; every
// field is vec4/mat4/ivec4-sized so std140 offsets equal C offsets, same discipline as SgUbo in
// zelda3d_sg_ubo.h). Deliberately sized to fit Zelda3DSg::kCommonBytes (which grows as SgUbo gains fields; the
// static_assert below enforces the match) exactly so a unified draw reuses the EXISTING
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
    // PICA constant-color fallback plus a byte-identical mirror of SgUbo::uMatConst. Generic-TEV
    // draws use the full palette below; simple unified CMB draws retain this selected-slot value.
    float uMatConst[4];
    // Mirror of SgUbo::uSheen. Generic-TEV draws use .w for coordinator-1 mapping.
    float uSheen[4];
    // Mirror of SgUbo::uTex1Xf (coordinator-1 transform), live on generic-TEV draws.
    float uTex1Xf[4];
    // Mirror of SgUbo::uFog3d0/uFog3d1 (OoT3D PICA distance fog, title port — zelda3d_sg_ubo.h).
    // Size-parity padding today: the (default-off) unified path doesn't apply the 3DS fog yet;
    // wire these through UNIFIED_COMMON_UBO_BODY when the unified renderer takes over CMB draws.
    float uFog3d0[4];
    float uFog3d1[4];
    // Mirror of SgUbo::uSphRot0/1/2 (sphere-map view-rotation override, title wordmark
    // decorations — zelda3d_sg_ubo.h). Size-parity padding for the unified path, same
    // rationale as uMatConst/uSheen/uTex1Xf/uFog3d* above.
    float uSphRot0[4];
    float uSphRot1[4];
    float uSphRot2[4];
    // Mirror of SgUbo::uLitDif1/uLitDif2/uLightDir2 (per-light diffuse products + light2 dir,
    // #153 CmbVShader vertex-lit port — zelda3d_sg_ubo.h). Size-parity padding today: the
    // (default-off) unified path still runs its single-light uMatAmbient/uMatDiffuse form; wire
    // these through UNIFIED_COMMON_UBO_BODY when the unified renderer takes over CMB draws.
    float uLitDif1[4];
    float uLitDif2[4];
    float uLightDir2[4];
    // Mirror of SgUbo::uTevStages/uTevConst/uTex2Xf/uTevCtl (generic per-stage TEV,
    // render.multi-stage-tev — zelda3d_sg_ubo.h). Live on kGenericTev CMB draws.
    uint32_t uTevStages[6 * 4];
    uint32_t uTevConst[8];
    float uTex2Xf[4];
    float uTevCtl[4];
};

static_assert(sizeof(CommonUbo) == Zelda3DSg::kCommonBytes,
              "CommonUbo must byte-match unified_shader.cpp's kCommonUboBody AND Zelda3DSg::kCommonBytes "
              "— the whole point is reusing the existing DRAW_MODEL push path unchanged");

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
