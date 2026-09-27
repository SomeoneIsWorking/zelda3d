#include "fast/backends/unified_shader.h"
#include "fast/backends/zelda3d_tev_glsl.h"
#include "fast/unified_vtx.h"
#include "fast/unified_material.h" // UnifiedMaterial — ditto
#include "fast/unified_ubo.h"      // CommonUbo/UnifiedDrawUbo — static_assert-checked against kCommonUboBody here
#include "fast/zelda3d_model_types.h"

#include <prism/processor.h>

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <mutex>

// See unified_shader.h for the design rationale. This file has ONE job right now: produce valid
// GLSL for the structural variants, sharing one combiner-evaluation function (evalInput /
// evalCycle) that reads UnifiedMaterial.combMux's SHADER_* operand codes at RUNTIME via a switch,
// instead of the old approach of baking a different GLSL expression per combiner permutation
// (gfx_sdl3gpu.cpp's sg_shader_item_to_str/sg_append_formula, invoked once per unique
// ColorCombinerKey). That old per-permutation text generation is left completely untouched —
// nothing here calls it or is called by it. Phase 2/3 decide how content routes to old vs new.
//
// Uses the SAME prism template engine as the N64 shader generator (gfx_sdl3gpu.cpp's
// kSgShaderTemplate) instead of hand-rolled string concatenation, for consistency — one combined
// @if(VERTEX_SHADER)/@else template for both stages, @if-gated structural features. Unlike the N64
// template, there's no @for over a dynamic attribute/input count here: UnifiedVtx's attribute set
// is FIXED regardless of variant (that's the whole point of collapsing hundreds of permutations
// into six structural buckets) — only a handful of @if-gated declarations/statements vary.

namespace Fast::Unified {

namespace {

// Local glslang compile helper — deliberately NOT sharing gfx_sdl3gpu.cpp's anonymous-namespace
// CompileGlslToSpirv (this module must compile standalone with zero live callers into the existing
// backend). Same settings: SPIR-V for SDL3 GPU's Vulkan driver.
std::once_flag gGlslangOnce;

bool CompileGlslToSpirv(EShLanguage stage, const std::string& src, std::vector<uint32_t>& outSpirv,
                        std::string& outLog) {
    std::call_once(gGlslangOnce, []() { glslang::InitializeProcess(); });

    glslang::TShader shader(stage);
    const char* str = src.c_str();
    shader.setStrings(&str, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);

    const TBuiltInResource* resources = GetDefaultResources();
    const int defaultVersion = 450;
    // glslang exposes EShMessages as a bit-mask enum; the combined flags are valid even though
    // the combined value is not listed as a standalone enumerator.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);

    if (!shader.parse(resources, defaultVersion, false, messages)) {
        outLog = std::string("parse: ") + shader.getInfoLog() + "\n" + shader.getInfoDebugLog();
        return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        outLog = std::string("link: ") + program.getInfoLog();
        return false;
    }
    glslang::SpvOptions spvOptions;
    spvOptions.disableOptimizer = true;
    spvOptions.validate = false;
    glslang::GlslangToSpv(*program.getIntermediate(stage), outSpirv, &spvOptions);
    return !outSpirv.empty();
}

struct VariantFeatures {
    bool hasTex0, hasTex1, hasTex2, alphaTest, fog, grayscale, genericTev, fog3d;
};

// Every variant carries the PICA fog block, not a subset. The gate is RUNTIME (uFogCtl.x), so
// compiling it everywhere costs one vec3 varying and a few instructions, while compiling it
// selectively would make the compiled set a SECOND fog policy that silently drops the fog for
// whichever material happens to land on a variant someone forgot — the exact class of bug the
// uFogColor gate already had. The N64 route is unaffected: its draws submit uFogCtl.x == 0.
constexpr bool kAllVariantsFog3d = true;

VariantFeatures FeaturesFor(Variant v) {
    switch (v) {
        case Variant::kUntextured:
            return { false, false, false, false, false, false, false, kAllVariantsFog3d };
        case Variant::kSingleTex:
            return { true, false, false, false, false, false, false, kAllVariantsFog3d };
        case Variant::kSingleTexAlphaTest:
            return { true, false, false, true, false, false, false, kAllVariantsFog3d };
        case Variant::kDualTex:
            return { true, true, false, false, false, false, false, kAllVariantsFog3d };
        case Variant::kDualTexFog:
            return { true, true, false, false, true, false, false, kAllVariantsFog3d };
        case Variant::kGrayscale:
            return { true, false, false, false, false, true, false, kAllVariantsFog3d };
        case Variant::kGenericTev:
            return { true, true, true, false, false, false, true, kAllVariantsFog3d };
        default:
            return {};
    }
}

std::optional<std::string> IncludeNoop(const std::string&) {
    return std::nullopt;
}

// One combined template for both stages, in the same style as gfx_sdl3gpu.cpp's kSgShaderTemplate
// (@if(VERTEX_SHADER)/@else split). The combined per-draw UBO ("UnifiedCommon") is the SAME block
// shared by both stages (matching the existing DRAW_MODEL op's push convention: identical bytes go
// to vertex binding 0 AND fragment binding 0; bones are a separate block at vertex binding 1) —
// // BYTE-IDENTICAL to unified_ubo.h's CommonUbo, sized to fit Zelda3DSg::kCommonBytes (static_assert-
// bytes) exactly so the existing DRAW_MODEL Op/AppendZelda3DModelDraw/mSoh3dModelUbos plumbing needs
// ZERO changes for a unified draw — see zelda3d_sdl3gpu_pass.cpp/gfx_sdl3gpu.cpp's DrawModel/DrawTriangles
// wiring.
const char* kUnifiedShaderTemplate = R"PRISM(@prism(type='fragment', name='Unified Shader', version='1.0.0')
#version 450

#define UNIFIED_COMMON_UBO_BODY \
    mat4 uMvp; \
    mat4 uMv; \
    vec4 uLightDir; \
    ivec4 uCombA[4]; \
    vec4 uPrimColor; \
    vec4 uEnvColor; \
    vec4 uFogColor; \
    vec4 uParams0; \
    vec4 uParams1; \
    vec4 uMatAmbient; \
    vec4 uMatDiffuse; \
    vec4 uPrimaryCtl; \
    vec4 uMatConst; \
    vec4 uSheen; \
    vec4 uTex0Xf; \
    vec4 uTex1Xf; \
    vec4 uFog3d0; \
    vec4 uFog3d1; \
    vec4 uSphNrm0; \
    vec4 uSphNrm1; \
    vec4 uSphNrm2; \
    vec4 uLitDif1; \
    vec4 uLitDif2; \
    vec4 uLightDir2; \
    uvec4 uTevStages[6]; \
    uvec4 uTevConst[2]; \
    vec4 uTex2Xf; \
    vec4 uTevCtl; \
    vec4 uFogCtl; \
    vec4 uDebug;

@if(VERTEX_SHADER)
    layout(location=0) in vec4 aPos;
    layout(location=1) in vec3 aNrm;
    layout(location=2) in vec2 aUv0;
    layout(location=3) in vec2 aUv1;
    layout(location=4) in vec4 aTexClamp;
    layout(location=5) in vec4 aColor0;
    layout(location=6) in vec4 aColor1;
    layout(location=7) in vec4 aColor2;
    layout(location=8) in vec4 aColor3;
    layout(location=9) in vec2 aFog;
    layout(location=10) in vec4 aBoneId;
    layout(location=11) in vec4 aBoneW;
    layout(location=12) in vec2 aUv2;

    layout(location=0) out vec2 vUv0;
    layout(location=1) out vec2 vUv1;
    layout(location=2) out vec4 vTexClamp;
    layout(location=3) out vec4 vColor0;
    layout(location=4) out vec4 vColor1;
    layout(location=5) out vec4 vColor2;
    layout(location=6) out vec4 vColor3;
    layout(location=7) out vec2 vFog;
    layout(location=8) out vec3 vNrmView;
    layout(location=9) out vec2 vUv2;
    // PICA distance-fog depth input. The fog factor is per FRAGMENT because the 3DS indexes its
    // 128-entry LUT by the z-buffer DEPTH of the fragment, not by a per-vertex distance; two
    // vertex-level variants of this were measured and falsified on the native path, so this
    // varying carries the world POSITION (affine in the interpolation, hence exact) and the dot
    // product is taken per fragment. Only the fogged variants pay for it.
    @if(o_fog3d) layout(location=10) out vec3 vWorld;

    layout(set=1, binding=0, std140) uniform UnifiedCommon { UNIFIED_COMMON_UBO_BODY } ubo;
    layout(set=1, binding=1, std140) uniform UnifiedBones { mat4 uBones[@{ZELDA3D_GL_MAX_BONES}]; } bones;

    void main() {
        vec3 sp = aPos.xyz;
        vec3 nM = aNrm;
        // 3DS GPU 4-bone blend (the plan's non-goal: this mechanism itself stays as-is, only the
        // vertex format/shader it feeds is unified). N64 content sets uParams1.z=0 and writes
        // identity bone data, so this branch is simply skipped.
        if (ubo.uParams1.z > 0.5) {
            vec4 acc = vec4(0.0); vec3 nAcc = vec3(0.0);
            for (int i = 0; i < 4; i++) {
                acc += aBoneW[i] * (bones.uBones[int(aBoneId[i])] * vec4(aPos.xyz, 1.0));
                nAcc += aBoneW[i] * (mat3(bones.uBones[int(aBoneId[i])]) * aNrm);
            }
            sp = acc.xyz;
            nM = nAcc;
        }
        // alreadyTransformed (N64): pos is already clip-space with a real perspective w — pass
        // through verbatim, do NOT multiply by uMvp (which would double-transform and also
        // clobber w=1). Otherwise (3DS): GPU-transform model-space pos via uMvp as before.
        gl_Position = (ubo.uParams1.w > 0.5) ? aPos : (ubo.uMvp * vec4(sp, 1.0));
        vNrmView = mat3(ubo.uMv) * nM;
        // World position for the PICA distance fog, mirroring the native path's vWorld exactly:
        // the model transform only, because for scene draws the camera is folded into uMvp and the
        // fog's eye depth is recovered by dotting this against uFog3d1 (camera forward + its offset).
        @if(o_fog3d)
            vWorld = (ubo.uMv * vec4(sp, 1.0)).xyz;
        @end
        @if(o_cmbExtraTex)
            vec3 ns = (ubo.uSphNrm0.w > 0.5)
                ? vec3(dot(ubo.uSphNrm0.xyz, nM), dot(ubo.uSphNrm1.xyz, nM), dot(ubo.uSphNrm2.xyz, nM))
                : (mat3(ubo.uMv) * nM);
            if (ubo.uSheen.w > 2.5 && ubo.uSheen.w < 3.5) {
                vec3 nv0 = normalize(ns);
                vec2 suv0 = vec2((nv0.x * 0.5 + 0.5 - ubo.uTex0Xf.z) * ubo.uTex0Xf.x,
                                 (nv0.y * 0.5 + 0.5 - ubo.uTex0Xf.w) * ubo.uTex0Xf.y);
                vUv0 = vec2(suv0.x, 1.0 - suv0.y);
            } else {
                vUv0 = vec2(aUv0.x, 1.0 - aUv0.y);
            }
            if (ubo.uTevCtl.y > 2.5 && ubo.uTevCtl.y < 3.5) {
                vec3 nv = normalize(ns);
                vec2 suv = vec2((nv.x * 0.5 + 0.5 - ubo.uTex1Xf.z) * ubo.uTex1Xf.x,
                                (nv.y * 0.5 + 0.5 - ubo.uTex1Xf.w) * ubo.uTex1Xf.y);
                vUv1 = vec2(suv.x, 1.0 - suv.y);
            } else {
                vec2 uv1 = vec2((aUv1.x - ubo.uTex1Xf.z) * ubo.uTex1Xf.x,
                                (aUv1.y - ubo.uTex1Xf.w) * ubo.uTex1Xf.y);
                vUv1 = vec2(uv1.x, 1.0 - uv1.y);
            }
            @if(o_genericTev)
            if (ubo.uTevCtl.z > 2.5 && ubo.uTevCtl.z < 3.5) {
                vec3 nv2 = normalize(ns);
                vec2 suv2 = vec2((nv2.x * 0.5 + 0.5 - ubo.uTex2Xf.z) * ubo.uTex2Xf.x,
                                 (nv2.y * 0.5 + 0.5 - ubo.uTex2Xf.w) * ubo.uTex2Xf.y);
                vUv2 = vec2(suv2.x, 1.0 - suv2.y);
            } else {
                vec2 uv2 = vec2((aUv2.x - ubo.uTex2Xf.z) * ubo.uTex2Xf.x,
                                (aUv2.y - ubo.uTex2Xf.w) * ubo.uTex2Xf.y);
                vUv2 = vec2(uv2.x, 1.0 - uv2.y);
            }
            @else
                vUv2 = vec2(0.0);
            @end
        @else
            vUv0 = aUv0;
            vUv1 = aUv1;
            vUv2 = vec2(0.0);
        @end
        vTexClamp = aTexClamp;
        // Exact CmbVShader unlit PRIMARY choice (shbin words 112--120). Model-space CMB
        // lighting modes 0/1 use authored MatDiffuse when the draw has no color attribute;
        // HasColor=1 replaces it with aColor. N64 and vertex-lit CMB routes retain aColor.
        vColor0 = (ubo.uParams1.w < 0.5 && ubo.uParams0.y < 1.5 && ubo.uPrimaryCtl.x < 0.5)
                    ? ubo.uMatDiffuse
                    : aColor0;
        vColor1 = aColor1;
        vColor2 = aColor2;
        vColor3 = aColor3;
        vFog = aFog;
        // Native CMB per-draw RGB modulation, mirroring the native path's UNCONDITIONAL
        // `vec3 shade = ubo.uTintSkin.xyz` flat modulator. Two exclusions, and only two:
        //   - alreadyTransformed (uParams1.w): an N64 draw uses uPrimColor as a combiner source,
        //     not as a modulation, so it must not be multiplied.
        //   - lightingMode 2 (uParams0.y): vertexLighting=1 takes PRIMARY from the PICA light
        //     bank, which the modulation would double-apply.
        // The `lit` term is deliberately NOT a condition. `lit` means "apply the character/prop
        // lighting term", which is a different question from "apply this draw's RGB modulation",
        // and ZELDA3D_HANDLE_FORCE_UNLIT suppresses the first without touching the second. Gating
        // on `lit` silently dropped the modulation for every lit=0 CMB draw: scene geometry
        // (Zelda3D_SceneTint hands it a real ambient-tinted value) and the title fire-glow (the
        // orange ConstColor from g_title_fire.cmab). The fire-glow is additively blended, so an
        // untinted white texture saturated all three channels where the oracle's amber kept blue
        // at 58% of red -- measured, not inferred.
        if (ubo.uParams1.w < 0.5 && ubo.uParams0.y < 1.5) {
            vColor0.rgb *= ubo.uPrimColor.rgb;
        }
        // The title wordmark is deliberately submitted force-unlit so world lighting cannot
        // contaminate it, but its draw function still binds one private CmbVShader light:
        // PRIMARY = vertexColor * (0.18 + max(0, N dot -L)). uSheen.x is the private ambient
        // and its nonzero value is the independent gate. This must run before the generic TEV
        // evaluator reads PRIMARY; keying it on lightingMode dropped the RE'd sheen from every
        // force-unlit title material when the unified route became the default.
        if (ubo.uSheen.x > 0.0) {
            vec3 nV = normalize(vNrmView);
            float ndotl = max(dot(nV, -normalize(ubo.uLightDir.xyz)), 0.0);
            vColor0 = vec4(clamp(vColor0.rgb * (ubo.uSheen.x + ndotl), 0.0, 1.0), vColor0.a);
        // lightingMode 2 (3DS CMB vertex lighting): bake PRIMARY here per vertex, before
        // interpolation. CmbVShader uses the normal after skinning and the draw transform, then
        // accumulates every enabled light slot. Actor draws bind the opposed two-light bank
        // (+D/light2Color, -D/light1Color) with ambient in the first slot only; scene draws bind
        // ambient in both slots with zero diffuse. The CPU packer has already reduced each bank to
        // uMatAmbient plus the two per-light diffuse products, exactly like the native CMB path.
        } else if (ubo.uParams0.y > 1.5) {
            vec3 nV = normalize(vNrmView);
            // Light directions are already normalized by the scene-light submission. Do not
            // normalize them here: CmbVShader dots the uniform verbatim, and a disabled/zero
            // direction must contribute zero rather than normalize(0) producing an undefined value.
            vec3 lit = ubo.uMatAmbient.xyz
                     + ubo.uLitDif1.rgb * max(dot(nV, -ubo.uLightDir.xyz), 0.0)
                     + ubo.uLitDif2.rgb * max(dot(nV, -ubo.uLightDir2.xyz), 0.0);
            float litAlpha = ubo.uLitDif1.a + ubo.uLitDif2.a;
            vec4 primary = vec4(lit, litAlpha);
            if (ubo.uPrimaryCtl.x > 0.5) primary *= aColor0;
            // Clamp order = the PRODUCT (PICA clamps o1 on register write). clamp(lit) first
            // was tried 2026-07-22 and measured ~30% dark vs the oracle — see the kFrag
            // comment in zelda3d_sdl3gpu_shaders.cpp. Do not re-flip.
            vColor0 = min(abs(primary), vec4(1.0));
        }
    }
@else
    layout(location=0) in vec2 vUv0;
    layout(location=1) in vec2 vUv1;
    layout(location=2) in vec4 vTexClamp;
    layout(location=3) in vec4 vColor0;
    layout(location=4) in vec4 vColor1;
    layout(location=5) in vec4 vColor2;
    layout(location=6) in vec4 vColor3;
    layout(location=7) in vec2 vFog;
    layout(location=8) in vec3 vNrmView;
    layout(location=9) in vec2 vUv2;
    @if(o_fog3d) layout(location=10) in vec3 vWorld;
    layout(location=0) out vec4 fragColor;

    @if(o_tex0) layout(set=2, binding=0) uniform sampler2D uTex0;
    @if(o_tex2) layout(set=2, binding=1) uniform sampler2D uTex2;
    @if(o_tex2)
        layout(set=2, binding=2) uniform sampler2D uTex1;
    @else
        @if(o_tex1) layout(set=2, binding=1) uniform sampler2D uTex1;
    @end

    layout(set=3, binding=0, std140) uniform UnifiedCommon { UNIFIED_COMMON_UBO_BODY } ubo;

    // SHADER_* operand codes (interpreter.h) — kept in sync manually; combMux is populated with
    // these exact values so N64's CCFeatures.c[2][2][4] can be copied in verbatim.
    const int SHADER_0 = 0;
    const int SHADER_INPUT_1 = 1;
    const int SHADER_INPUT_2 = 2;
    const int SHADER_INPUT_3 = 3;
    const int SHADER_INPUT_4 = 4;
    const int SHADER_TEXEL0 = 8;
    const int SHADER_TEXEL0A = 9;
    const int SHADER_TEXEL1 = 10;
    const int SHADER_TEXEL1A = 11;
    const int SHADER_1 = 12;
    const int SHADER_COMBINED = 13;
    const int SHADER_NOISE = 14;

    float random(in vec3 value) {
        float r = dot(sin(value), vec3(12.9898, 78.233, 37.719));
        return fract(sin(r) * 143758.5453);
    }

    // RE'd 3DS fog LUT node value at t = i/128 (FogResUpdater, linear mode 0 —
    // oot3d-decomp title_env_lighting.md §13): eyeDist = b/(a - t) (inverse projection), then the
    // linear fogNear..fogFar window. uFog3d0 = (a, b, fogNear, fogFar). Byte-for-byte the native
    // path's fog3dNode(); keeping one copy of the formula per route rather than one per pipeline
    // would be the second, divergent implementation this codebase keeps refusing to add, so if the
    // recovered formula changes, change it in both (zelda3d_sdl3gpu_shaders.cpp) and re-measure.
    @if(o_fog3d)
        float fog3dNode(float t) {
            float d = ubo.uFog3d0.y / max(ubo.uFog3d0.x - t, 1e-6);
            if (d < ubo.uFog3d0.z) return 1.0;
            if (d > ubo.uFog3d0.w) return 0.0;
            return (ubo.uFog3d0.w - d) / (ubo.uFog3d0.w - ubo.uFog3d0.z);
        }
    @end

    vec4 texel0() {
        @if(o_tex0)
            return texture(uTex0, clamp(vUv0, 0.5 / vec2(textureSize(uTex0, 0)), vTexClamp.xy));
        @else
            return vec4(1.0);
        @end
    }
    vec4 texel1() {
        @if(o_tex1)
            return texture(uTex1, clamp(vUv1, 0.5 / vec2(textureSize(uTex1, 0)), vTexClamp.zw));
        @else
            return vec4(1.0);
        @end
    }
    @if(o_genericTev)
    vec4 texel2() { return texture(uTex2, vUv2); }

    @{generic_tev_functions}
    @end

    // One generic evaluator for both cycles/both RGB+alpha — replaces the old per-permutation
    // sg_shader_item_to_str/sg_append_formula text generation with a runtime switch. "single" /
    // "multiply" / "mix" are not distinct formulas (see unified_material.h) — this is always the
    // fully general (A-B)*C+D, so there is exactly one code path regardless of combiner shape.
    vec4 evalInput(int code, vec4 combined) {
        if (code == SHADER_0) return vec4(0.0);
        if (code == SHADER_1) return vec4(1.0);
        if (code == SHADER_INPUT_1) return vColor0;
        if (code == SHADER_INPUT_2) return vColor1;
        if (code == SHADER_INPUT_3) return vColor2;
        if (code == SHADER_INPUT_4) return vColor3;
        if (code == SHADER_TEXEL0) return texel0();
        if (code == SHADER_TEXEL0A) return vec4(texel0().a);
        if (code == SHADER_TEXEL1) return texel1();
        if (code == SHADER_TEXEL1A) return vec4(texel1().a);
        if (code == SHADER_COMBINED) return combined;
        if (code == SHADER_NOISE) {
            float n = (random(vec3(floor(gl_FragCoord.xy * ubo.uParams1.x), ubo.uParams0.w)) + 1.0) / 2.0;
            return vec4(n);
        }
        return vec4(0.0);
    }
    vec4 evalCycle(int cycle, vec4 combined) {
        vec4 a = evalInput(ubo.uCombA[cycle * 2 + 0][0], combined);
        vec4 b = evalInput(ubo.uCombA[cycle * 2 + 0][1], combined);
        vec4 c = evalInput(ubo.uCombA[cycle * 2 + 0][2], combined);
        vec4 d = evalInput(ubo.uCombA[cycle * 2 + 0][3], combined);
        vec4 aA = evalInput(ubo.uCombA[cycle * 2 + 1][0], combined);
        vec4 bA = evalInput(ubo.uCombA[cycle * 2 + 1][1], combined);
        vec4 cA = evalInput(ubo.uCombA[cycle * 2 + 1][2], combined);
        vec4 dA = evalInput(ubo.uCombA[cycle * 2 + 1][3], combined);
        vec3 rgb = (a.rgb - b.rgb) * c.rgb + d.rgb;
        float alpha = (aA.a - bA.a) * cA.a + dA.a;
        return vec4(rgb, alpha);
    }

    void main() {
        @if(o_genericTev)
            vec4 fragPrimary = ubo.uPrimaryCtl.y > 0.5 ? vColor0 : vec4(0.0);
            vec4 fragSecondary = vec4(0.0);
            vec4 texel = tevRun(vColor0, fragPrimary, fragSecondary, texel0(), texel1(), texel2());
            int afn = int(ubo.uTevCtl.w + 0.5);
            if (afn > 0 && !alphaPass(texel.a, ubo.uParams0.x, afn - 1)) discard;
        @else
        @if(o_cmbDualTex)
            vec4 primary = vColor0;
            vec4 t0 = texel0();
            vec3 t1 = texel1().rgb;
            vec3 dualRgb;
            if (ubo.uSheen.y > 2.5) {
                dualRgb = clamp(t0.rgb * t1 * ubo.uSheen.z, 0.0, 1.0);
            } else if (ubo.uSheen.y > 1.5) {
                dualRgb = clamp(t0.rgb + t1, 0.0, 1.0);
            } else {
                dualRgb = clamp(t0.rgb + t1, 0.0, 1.0) * t0.rgb;
            }
            vec4 texel = vec4(dualRgb * primary.rgb, t0.a * primary.a);
        @else
            vec4 texel = evalCycle(0, vec4(0.0));
            if (ubo.uParams0.z > 1.5) texel = evalCycle(1, texel); // cycleCount==2
        @end
        @end

        // Selected-draw diagnostics mirror the native CMB FRAGDBG meanings. They are compiled only
        // when requested before renderer startup and remain gated per draw by uDebug.x.
        @if(o_probeTex0)
            if (ubo.uDebug.x > 0.5) { gl_FragDepth = 0.0; fragColor = vec4(texel0().rgb, 1.0); return; }
        @end
        @if(o_probePrimary)
            if (ubo.uDebug.x > 0.5) { gl_FragDepth = 0.0; fragColor = vec4(vColor0.rgb, 1.0); return; }
        @end
        @if(o_probeCombined)
            if (ubo.uDebug.x > 0.5) { gl_FragDepth = 0.0; fragColor = vec4(texel.rgb, 1.0); return; }
        @end

        // lightingMode 1 (3DS character half-Lambert): applied HERE, not baked into vColor0 like
        // mode 2, because the combiner's SHADER_INPUT_1 must stay the raw per-vertex tint (matching
        // what the old fixed CMB shader's `t.rgb * vColor.rgb * shade` does — shade multiplies the
        // combiner OUTPUT, it isn't itself a combiner input). Modes 0/2 need no fragment action.
        if (ubo.uParams0.y > 0.5 && ubo.uParams0.y < 1.5) {
            float hl = dot(normalize(vNrmView), normalize(ubo.uLightDir.xyz)) * 0.5 + 0.5;
            texel.rgb *= (0.55 + 0.45 * hl);
        }

        @if(o_grayscale)
            { float g = dot(texel.rgb, vec3(0.299, 0.587, 0.114)); texel.rgb = vec3(g); }
        @end
        @if(o_alphaTest)
            // uParams0.x < 0 is the "texture-edge" sentinel (N64 opt_texture_edge: snap to opaque
            // above the fixed 0.19 threshold, discard below — a different rule from a real alpha
            // ref compare, not just a different constant). See unified_n64_pack.cpp.
            if (ubo.uParams0.x < 0.0) {
                if (texel.a > 0.19) texel.a = 1.0; else discard;
            } else {
                if (texel.a < ubo.uParams0.x) discard;
            }
        @end
        @if(o_fog)
            texel.rgb = mix(texel.rgb, ubo.uFogColor.rgb, clamp(vFog.x, 0.0, 1.0));
        @end
        // OoT3D PICA distance fog (uFogCtl.x == 2.0 — zelda3d_sg_ubo.h, the same per-draw gate the
        // native path reads as uFog.w, carried verbatim rather than re-derived). This is a SEPARATE
        // mode from the N64 ramp above, not a replacement: the native path tests uFog.w > 1.5 first
        // and falls through to the F3DEX ramp only when the PICA gate is off, and the two routes
        // share one mode enum, so a draw can be moved between them by one number.
        //
        // The 3DS indexes a 128-entry fog LUT by the z-buffer DEPTH of this fragment. The depth is
        // recovered from the interpolated world position: d = eye depth along the view axis, then
        // depth = a - b/d is the fragment's z/w under the 3DS projection — exactly the value PICA
        // indexes with. PICA then samples the entry and LERPs toward the next INSIDE the entry; with
        // the scene's compressed depth range entry 127 spans eye ~873..zFar, so this
        // piecewise-linear-in-DEPTH interpolation, not the underlying distance curve, is the visible
        // haze. (The 3DS's 11/13-bit LUT quantization is omitted: <=1/2048 in the factor, sub-LSB of
        // the 8-bit output.) Never applied to sky (uLightDir[3]), on this route or the native one.
        @if(o_fog3d)
            if (ubo.uFogCtl.x > 1.5 && ubo.uLightDir[3] < 0.5) {
                float d3 = dot(vWorld, ubo.uFog3d1.xyz) - ubo.uFog3d1.w;
                float depth3ds = ubo.uFog3d0.x - ubo.uFog3d0.y / max(d3, 1e-3);
                float x = clamp(depth3ds, 0.0, 1.0) * 128.0;
                float i0 = min(floor(x), 127.0);
                float f0 = fog3dNode(i0 * (1.0 / 128.0));
                float f1 = fog3dNode((i0 + 1.0) * (1.0 / 128.0));
                float factor = clamp(f0 + (f1 - f0) * (x - i0), 0.0, 1.0);
                texel.rgb = mix(ubo.uFogColor.rgb, texel.rgb, factor);
            }
        @end

        // Native CMB applies the caller's draw alpha after TEV and alpha-test. Applying it to
        // PRIMARY earlier would incorrectly change the alpha-test decision during title fades.
        if (ubo.uParams1.w < 0.5) texel.a *= ubo.uPrimColor.a;
        texel = clamp(texel, 0.0, 1.0);
        fragColor = texel;
    }
@end
)PRISM";

std::string BuildSource(Variant v, bool vertex, int fragmentProbeMode = 0) {
    VariantFeatures f = FeaturesFor(v);
    prism::Processor processor;
    prism::ContextItems ctx = {
        { "VERTEX_SHADER", vertex },
        { "o_tex0", f.hasTex0 },
        { "o_tex1", f.hasTex1 },
        { "o_tex2", f.hasTex2 },
        { "o_alphaTest", f.alphaTest },
        { "o_fog", f.fog },
        { "o_fog3d", f.fog3d },
        { "o_grayscale", f.grayscale },
        { "o_genericTev", f.genericTev },
        { "o_cmbExtraTex", f.genericTev || v == Variant::kDualTex || v == Variant::kDualTexFog },
        { "o_cmbDualTex", v == Variant::kDualTex || v == Variant::kDualTexFog },
        { "o_probeTex0", !vertex && fragmentProbeMode == 1 },
        { "o_probePrimary", !vertex && fragmentProbeMode == 5 },
        { "o_probeCombined", !vertex && fragmentProbeMode == 6 },
        { "generic_tev_functions", Fast::Zelda3DTev::kGenericFunctions },
        { "ZELDA3D_GL_MAX_BONES", ZELDA3D_GL_MAX_BONES },
    };
    processor.populate(ctx);
    processor.load(kUnifiedShaderTemplate);
    processor.bind_include_loader(IncludeNoop);
    return processor.process();
}

} // namespace

// Mirrors the @if(o_tex0/o_tex1/o_tex2) sampler block in the fragment template: tex0 is binding 0,
// and the second slot is uTex1 at binding 1 unless the generic-TEV variant is present, which adds a
// third at binding 2.
uint32_t VariantSamplerCount(Variant v) {
    const VariantFeatures f = FeaturesFor(v);
    return (f.hasTex0 ? 1u : 0u) + (f.hasTex2 ? 2u : (f.hasTex1 ? 1u : 0u));
}

const char* VariantName(Variant v) {
    switch (v) {
        case Variant::kUntextured:
            return "Untextured";
        case Variant::kSingleTex:
            return "SingleTex";
        case Variant::kSingleTexAlphaTest:
            return "SingleTexAlphaTest";
        case Variant::kDualTex:
            return "DualTex";
        case Variant::kDualTexFog:
            return "DualTexFog";
        case Variant::kGrayscale:
            return "Grayscale";
        case Variant::kGenericTev:
            return "GenericTev";
        default:
            return "?";
    }
}

std::string BuildVertexSource(Variant v) {
    return BuildSource(v, true);
}

std::string BuildFragmentSource(Variant v) {
    return BuildSource(v, false);
}

std::string BuildFragmentSource(Variant v, int fragmentProbeMode) {
    return BuildSource(v, false, fragmentProbeMode);
}

bool SelfTestUnifiedShaderVariants(std::string& outLog) {
    bool allOk = true;
    for (int i = 0; i < (int)Variant::kCount; i++) {
        Variant v = (Variant)i;
        std::vector<uint32_t> spirv;
        std::string log;
        if (!CompileGlslToSpirv(EShLangVertex, BuildVertexSource(v), spirv, log)) {
            outLog += std::string("[") + VariantName(v) + " vertex] " + log + "\n";
            allOk = false;
        }
        if (!CompileGlslToSpirv(EShLangFragment, BuildFragmentSource(v), spirv, log)) {
            outLog += std::string("[") + VariantName(v) + " fragment] " + log + "\n";
            allOk = false;
        }
    }
    return allOk;
}

} // namespace Fast::Unified
