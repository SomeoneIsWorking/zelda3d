#pragma once

#include <string>
#include <vector>

// Render-unification effort (kanban #131), Phase 1: the unified shader that will eventually
// replace both the N64 per-combiner-permutation shader generator (gfx_sdl3gpu.cpp's
// kSgShaderTemplate) and the fixed 3DS CMB shader (zelda3d_sdl3gpu.cpp's kVert/kFrag), per
// UnifiedVtx (unified_vtx.h) / UnifiedMaterial (unified_material.h).
//
// Phase 1 status: DORMANT. This module is unreferenced by any live draw path — nothing calls
// GetOrCreateUnifiedPipeline yet. SelfTestUnifiedShaderVariants() is the only thing exercising it,
// gated behind ZELDA3D_UNIFIED_SHADER_SELFTEST=1, purely to catch a GLSL authoring mistake at
// compile-shader time rather than letting bad source sit unvalidated until Phase 2 wires it in.
namespace Fast::Unified {

// The plan calls for "~3-6 statically compiled shader variants keyed on structural features only
// (texture count, alpha-on/off, fog, grayscale)" rather than one variant per combiner permutation
// (hundreds) or one fully generic uber-shader (correct but slower on simple draws). These six cover
// the structural buckets seen in the Phase 0 corpus sweep (scratch/render_unify/cc_corpus.log):
enum class Variant {
    kUntextured,        // vertex-color-only draws (solid UI fills, debug wireframes)
    kSingleTex,          // 1 texture, no alpha test, no fog — the common opaque case
    kSingleTexAlphaTest, // 1 texture with alpha-threshold/edge discard (cutout foliage, decals)
    kDualTex,            // 2 textures / 2-cycle combine, no fog
    kDualTexFog,         // 2 textures / 2-cycle combine, with fog blend
    kGrayscale,          // N64 grayscale-filter combiner mode (opt_grayscale)
    kCount
};

const char* VariantName(Variant v);

// Returns the GLSL source for one stage of one variant. The combiner arithmetic itself is NOT
// baked per-variant text (that's the old per-permutation-compiled approach this replaces) — it's a
// runtime switch over UnifiedMaterial.combMux's SHADER_* operand codes, shared verbatim across all
// six variants. Only structural feature (texture count / alpha test / fog / grayscale) differs.
std::string BuildVertexSource(Variant v);
std::string BuildFragmentSource(Variant v);

// Compiles every variant's vertex + fragment source to SPIR-V via glslang, purely to validate the
// GLSL is well-formed (does NOT create any SDL_GPUShader/pipeline — that's Phase 2/3 wiring, which
// will reuse this same source through the existing CompileGlslToSpirv path in gfx_sdl3gpu.cpp).
// Returns true iff all variants compiled; failures are appended to outLog.
bool SelfTestUnifiedShaderVariants(std::string& outLog);

} // namespace Fast::Unified
