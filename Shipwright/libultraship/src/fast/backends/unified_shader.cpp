#include "fast/backends/unified_shader.h"
#include "fast/backends/zelda3d_tev_glsl.h"
#include "fast/unified_vtx.h"      // UnifiedVtx — not yet referenced by any draw path (Phase 2/3)
#include "fast/unified_material.h" // UnifiedMaterial — ditto
#include "fast/unified_ubo.h"      // CommonUbo/UnifiedDrawUbo — static_assert-checked against kCommonUboBody here
#include "fast/zelda3d_gl.h" // ZELDA3D_GL_MAX_BONES

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
    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

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
    bool hasTex0, hasTex1, hasTex2, alphaTest, fog, grayscale, genericTev;
};

VariantFeatures FeaturesFor(Variant v) {
    switch (v) {
        case Variant::kUntextured:         return { false, false, false, false, false, false, false };
        case Variant::kSingleTex:          return { true, false, false, false, false, false, false };
        case Variant::kSingleTexAlphaTest: return { true, false, false, true, false, false, false };
        case Variant::kDualTex:            return { true, true, false, false, false, false, false };
        case Variant::kDualTexFog:         return { true, true, false, false, true, false, false };
        case Variant::kGrayscale:          return { true, false, false, false, false, true, false };
        case Variant::kGenericTev:         return { true, true, true, false, false, false, true };
        default:                          return {};
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
// ZERO changes for a unified draw — see zelda3d_sdl3gpu.cpp/gfx_sdl3gpu.cpp's DrawModel/DrawTriangles
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
    vec4 uMatConst; \
    vec4 uSheen; \
    vec4 uTex1Xf; \
    vec4 uFog3d0; \
    vec4 uFog3d1; \
    vec4 uSphRot0; \
    vec4 uSphRot1; \
    vec4 uSphRot2; \
    vec4 uLitDif1; \
    vec4 uLitDif2; \
    vec4 uLightDir2; \
    uvec4 uTevStages[6]; \
    uvec4 uTevConst[2]; \
    vec4 uTex2Xf; \
    vec4 uTevCtl;

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
        @if(o_genericTev)
            vUv0 = vec2(aUv0.x, 1.0 - aUv0.y);
            vec3 ns = mat3(ubo.uMv) * nM;
            if (ubo.uSheen.w > 2.5) {
                vec3 nv = normalize(ns);
                vec2 suv = vec2((nv.x * 0.5 + 0.5 - ubo.uTex1Xf.z) * ubo.uTex1Xf.x,
                                (nv.y * 0.5 + 0.5 - ubo.uTex1Xf.w) * ubo.uTex1Xf.y);
                vUv1 = vec2(suv.x, 1.0 - suv.y);
            } else {
                vec2 uv1 = vec2((aUv0.x - ubo.uTex1Xf.z) * ubo.uTex1Xf.x,
                                (aUv0.y - ubo.uTex1Xf.w) * ubo.uTex1Xf.y);
                vUv1 = vec2(uv1.x, 1.0 - uv1.y);
            }
            if (ubo.uTevCtl.z > 2.5 && ubo.uTevCtl.z < 3.5) {
                vec3 nv2 = normalize(ns);
                vec2 suv2 = vec2((nv2.x * 0.5 + 0.5 - ubo.uTex2Xf.z) * ubo.uTex2Xf.x,
                                 (nv2.y * 0.5 + 0.5 - ubo.uTex2Xf.w) * ubo.uTex2Xf.y);
                vUv2 = vec2(suv2.x, 1.0 - suv2.y);
            } else {
                vec2 uv2 = vec2((aUv0.x - ubo.uTex2Xf.z) * ubo.uTex2Xf.x,
                                (aUv0.y - ubo.uTex2Xf.w) * ubo.uTex2Xf.y);
                vUv2 = vec2(uv2.x, 1.0 - uv2.y);
            }
        @else
            vUv0 = aUv0;
            vUv1 = aUv1;
            vUv2 = vec2(0.0);
        @end
        vTexClamp = aTexClamp;
        vColor0 = aColor0;
        vColor1 = aColor1;
        vColor2 = aColor2;
        vColor3 = aColor3;
        vFog = aFog;
        // lightingMode 2 (3DS scene vertex-lit): baked here per-vertex, matching the existing
        // per-vertex NdotL approach (docs/oot3d_world_lighting_re.md), not in the fragment stage.
        if (ubo.uParams0.y > 1.5) {
            // -uLightDir: uLightDir is 3DS-native light-TRAVEL (see Zelda3D_UpdateLight's
            // direction-convention comment); negate at the dot like CmbVShader / kFrag do.
            float ndotl = max(dot(normalize(nM), -normalize(ubo.uLightDir.xyz)), 0.0);
            vec3 lit = ubo.uMatAmbient.xyz + ubo.uMatDiffuse.xyz * ndotl;
            // Clamp order = the PRODUCT (PICA clamps o1 on register write). clamp(lit) first
            // was tried 2026-07-22 and measured ~30% dark vs the oracle — see the kFrag
            // comment in zelda3d_sdl3gpu.cpp. Do not re-flip.
            vColor0 = vec4(clamp(vColor0.rgb * lit, 0.0, 1.0), vColor0.a);
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
            vec4 texel = tevRun(vColor0, texel0(), texel1(), texel2());
            int afn = int(ubo.uTevCtl.w + 0.5);
            if (afn > 0 && !alphaPass(texel.a, ubo.uParams0.x, afn - 1)) discard;
        @else
            vec4 texel = evalCycle(0, vec4(0.0));
            if (ubo.uParams0.z > 1.5) texel = evalCycle(1, texel); // cycleCount==2
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

        texel = clamp(texel, 0.0, 1.0);
        fragColor = texel;
    }
@end
)PRISM";

std::string BuildSource(Variant v, bool vertex) {
    VariantFeatures f = FeaturesFor(v);
    prism::Processor processor;
    prism::ContextItems ctx = {
        { "VERTEX_SHADER", vertex },
        { "o_tex0", f.hasTex0 },
        { "o_tex1", f.hasTex1 },
        { "o_tex2", f.hasTex2 },
        { "o_alphaTest", f.alphaTest },
        { "o_fog", f.fog },
        { "o_grayscale", f.grayscale },
        { "o_genericTev", f.genericTev },
        { "generic_tev_functions", Fast::Zelda3DTev::kGenericFunctions },
        { "ZELDA3D_GL_MAX_BONES", ZELDA3D_GL_MAX_BONES },
    };
    processor.populate(ctx);
    processor.load(kUnifiedShaderTemplate);
    processor.bind_include_loader(IncludeNoop);
    return processor.process();
}

} // namespace

const char* VariantName(Variant v) {
    switch (v) {
        case Variant::kUntextured: return "Untextured";
        case Variant::kSingleTex: return "SingleTex";
        case Variant::kSingleTexAlphaTest: return "SingleTexAlphaTest";
        case Variant::kDualTex: return "DualTex";
        case Variant::kDualTexFog: return "DualTexFog";
        case Variant::kGrayscale: return "Grayscale";
        case Variant::kGenericTev: return "GenericTev";
        default: return "?";
    }
}

std::string BuildVertexSource(Variant v) {
    return BuildSource(v, true);
}

std::string BuildFragmentSource(Variant v) {
    return BuildSource(v, false);
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
