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
    // The per-draw texcoord SCROLL: the native applies it as `aUv + uExtra.yz` in the vertex stage
    // (a post-flip ADD, since it writes `1.0 - aUv.y + uExtra.z`), so it is added after the V flip
    // here too. Zero for every draw that did not ask for a scroll, which is why adding the term
    // unconditionally is safe rather than a second policy. It is what animates the OoT3D sky cloud
    // band per its .cmab rate (#28b), and it is per DRAW, so it cannot ride the per-group
    // coordinator transform in uTex0Xf.
    //
    // This field was the dead `uEnvColor`, written as 0 and read by nothing on either route.
    float uUvScroll[4];
    float uFogColor[4];
    float uParams0[4]; // x=alphaRef, y=lightingMode, z=cycleCount, w=frame_count
    float uParams1[4]; // x=noise_scale, y=polygonOffset, z=hasSkin, w=alreadyTransformed (N64)
    float uMatAmbient[4];
    float uMatDiffuse[4];
    float uPrimaryCtl[4]; // x=CmbVShader HasColor; remaining lanes reserved
    // PICA constant-color fallback plus a byte-identical mirror of SgUbo::uMatConst. Generic-TEV
    // draws use the full palette below; simple unified CMB draws retain this selected-slot value.
    float uMatConst[4];
    // Mirror of SgUbo::uSheen. CMB draws use .w for coordinator-0 mapping.
    float uSheen[4];
    // Mirror of SgUbo::uTex0Xf (coordinator-0 transform), live for sphere-mapped TEX0.
    float uTex0Xf[4];
    // Mirror of SgUbo::uTex1Xf (coordinator-1 transform), live on generic-TEV draws.
    float uTex1Xf[4];
    // Mirror of SgUbo::uFog3d0/uFog3d1 (OoT3D PICA distance fog, title port — zelda3d_sg_ubo.h).
    // Carried verbatim; the shader that consumes them is @if(o_fog3d) in the fragment main().
    float uFog3d0[4];
    float uFog3d1[4];
    // Mirror of SgUbo::uSphNrm0/1/2 (oracle uModelView normal transform for sphere mapping).
    float uSphNrm0[4];
    float uSphNrm1[4];
    float uSphNrm2[4];
    // Mirror of SgUbo::uLitDif1/uLitDif2/uLightDir2 (per-light RGBA diffuse products + light2 dir,
    // #153 CmbVShader vertex-lit port — zelda3d_sg_ubo.h). Live for unified CMB lightingMode 2:
    // actor draws require both opposed directional slots, while scene draws reduce to their
    // separately packed repeated ambient because both diffuse products are zero.
    float uLitDif1[4];
    float uLitDif2[4];
    float uLightDir2[4];
    // Mirror of SgUbo::uTevStages/uTevConst/uTex2Xf/uTevCtl (generic per-stage TEV,
    // render.multi-stage-tev — zelda3d_sg_ubo.h). Live on kGenericTev CMB draws.
    uint32_t uTevStages[6 * 4];
    uint32_t uTevConst[8];
    float uTex2Xf[4];
    float uTevCtl[4];
    // The per-draw fog MODE: the VALUE of SgUbo::uFog[3], assigned component-wise by the packer
    // (the two structs order their fields differently, so this is a value mirror, not byte parity
    // — do not memcpy it). One enum shared with the native path, not a second policy: 0 = no fog,
    // 1 = the F3DEX ramp (N64 draws; uFogColor + the aFog varying), 2 = the OoT3D PICA
    // distance-fog LUT (CMB draws; uFog3d0/uFog3d1 + uFogColor). Mode 2 also requires
    // uLightDir[3] < 0.5 — sky is excluded on both routes, so the flag rides the same lane the
    // native reads it from. uFogColor carries the colour for both modes.
    //
    // This replaced the uNativeLayoutPad filler that stood here when the unified route owned no
    // fog at all: a size-parity hole in the design, in a slot the same fog needed.
    float uFogCtl[4];
    float uDebug[4]; // Mirror of SgUbo::uDebug; renderer-only selected-fragment probe gate.
};

// Adapt the native CMB light-bank payload into the unified layout without re-deriving its actor /
// scene policy. The native packer is authoritative: uAmbient.w carries enabled-slot ambient
// multiplicity, and uLitDif1/2 carry the material-diffuse RGBA products for the bound slots.
inline void CopyCmbVertexLightBank(CommonUbo& target, const Zelda3DSg::SgUbo& source) {
    for (int component = 0; component < 3; ++component) {
        target.uMatAmbient[component] = source.uAmbient[component] * source.uAmbient[3];
    }
    target.uMatAmbient[3] = 0.0f;
    for (int component = 0; component < 4; ++component) {
        target.uMatDiffuse[component] = source.uMatDiffuse[component];
        target.uPrimaryCtl[component] = source.uPrimaryCtl[component];
        target.uLightDir[component] = source.uLightDir[component];
        target.uLitDif1[component] = source.uLitDif1[component];
        target.uLitDif2[component] = source.uLitDif2[component];
        target.uLightDir2[component] = source.uLightDir2[component];
    }
}

// Carry the per-draw fog MODE from the native packer. The frame-level fog PARAMETERS
// (uFog3d0/uFog3d1, the colour in uFogColor) are copied field by field because the two structs
// order their fields differently, and the gate is no different: copying only the parameters left
// every unified draw unfogged, because the shader's block is gated on the mode. One number, read
// from the same field the native shader reads, decides it on either route.
inline void PackCmbFogGate(CommonUbo& target, const Zelda3DSg::SgUbo& source) {
    target.uFogCtl[0] = source.uFog[3];
    target.uFogCtl[1] = 0.0f;
    target.uFogCtl[2] = 0.0f;
    target.uFogCtl[3] = 0.0f;
}

// Preserve the native CMB path's per-draw modulation without introducing a second UBO field set.
// uPrimColor is the N64 primitive color when alreadyTransformed is true; for model-space CMB draws
// it carries the caller's RGBA modulation, which the vertex stage applies on every CMB draw except
// the vertex-lit one (see the gate in BuildVertexSource).
//
// There is deliberately no "is the modulation enabled" parameter. The native authority applies the
// same modulation unconditionally (`shade = ubo.uTintSkin.xyz`), so any such switch here would be a
// second, divergent policy — and the one that was here keyed on `lit`, which silently dropped the
// modulation for every force-unlit and every scene-geometry draw. uParams1[0] is the N64 noise scale
// on the N64 route and is unused by CMB draws (their frame_count is 0, so SHADER_NOISE never reads
// it), so it is written explicitly rather than left to carry a stale meaning.
inline void PackCmbDrawModulation(CommonUbo& target, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    constexpr float kByteToFloat = 1.0f / 255.0f;
    target.uPrimColor[0] = static_cast<float>(red) * kByteToFloat;
    target.uPrimColor[1] = static_cast<float>(green) * kByteToFloat;
    target.uPrimColor[2] = static_cast<float>(blue) * kByteToFloat;
    target.uPrimColor[3] = static_cast<float>(alpha) * kByteToFloat;
    target.uParams1[0] = 0.0f;
}

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
