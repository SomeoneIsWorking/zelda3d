#include "fast/backends/unified_shader.h"
#include "fast/unified_vtx.h"      // UnifiedVtx — not yet referenced by any draw path (Phase 2/3)
#include "fast/unified_material.h" // UnifiedMaterial — ditto
#include "fast/soh3d_gl.h" // SOH3D_GL_MAX_BONES

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <mutex>

// See unified_shader.h for the design rationale. This file has ONE job right now: produce valid
// GLSL for the six structural variants, sharing one combiner-evaluation function (evalInput /
// evalCycle) that reads UnifiedMaterial.combMux's SHADER_* operand codes at RUNTIME via a switch,
// instead of the old approach of baking a different GLSL expression per combiner permutation
// (gfx_sdl3gpu.cpp's sg_shader_item_to_str/sg_append_formula, invoked once per unique
// ColorCombinerKey). That old per-permutation text generation is left completely untouched —
// nothing here calls it or is called by it. Phase 2/3 decide how content routes to old vs new.

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
    bool hasTex0, hasTex1, alphaTest, fog, grayscale;
};

VariantFeatures FeaturesFor(Variant v) {
    switch (v) {
        case Variant::kUntextured:        return { false, false, false, false, false };
        case Variant::kSingleTex:         return { true, false, false, false, false };
        case Variant::kSingleTexAlphaTest:return { true, false, true, false, false };
        case Variant::kDualTex:           return { true, true, false, false, false };
        case Variant::kDualTexFog:        return { true, true, false, true, false };
        case Variant::kGrayscale:         return { true, false, false, false, true };
        default:                          return {};
    }
}

// The material UBO layout shared by both stages (std140; field order matches UnifiedMaterial.h's
// documented layout, packed for GLSL rather than reinterpret-cast — Phase 2/3 write the CPU-side
// packer when this is actually wired to a live draw).
const char* kMaterialUboBody =
    "    ivec4 uCombA[4];\n"    // combMux[2][2][4] flattened, 16 ints
    "    vec4 uPrimColor;\n"
    "    vec4 uEnvColor;\n"
    "    vec4 uBlendColor;\n"
    "    vec4 uFogColor;\n"
    "    vec4 uParams0;\n"       // x=alphaRef, y=lightingMode, z=cycleCount, w=frame_count
    "    vec4 uParams1;\n"       // x=noise_scale, y=polygonOffset, z=hasSkin, w unused
    "    vec4 uMatAmbient;\n"    // xyz = matAmbient
    "    vec4 uMatDiffuse;\n";  // xyz = matDiffuse

const char* kXformUboBody =
    "    mat4 uMvp;\n"
    "    mat4 uMv;\n"
    "    vec4 uLightDir;\n"
    "    vec4 uVpParams;\n";

std::string VertexAttribs() {
    return
        "layout(location=0) in vec3 aPos;\n"
        "layout(location=1) in vec3 aNrm;\n"
        "layout(location=2) in vec2 aUv0;\n"
        "layout(location=3) in vec2 aUv1;\n"
        "layout(location=4) in vec4 aTexClamp;\n"
        "layout(location=5) in vec4 aColor0;\n"
        "layout(location=6) in vec4 aColor1;\n"
        "layout(location=7) in vec4 aColor2;\n"
        "layout(location=8) in vec4 aColor3;\n"
        "layout(location=9) in vec2 aFog;\n"
        "layout(location=10) in vec4 aBoneId;\n"
        "layout(location=11) in vec4 aBoneW;\n";
}

std::string Varyings(const char* dir) {
    std::string s;
    int loc = 0;
    auto line = [&](const char* type, const char* name) {
        s += "layout(location=" + std::to_string(loc++) + ") " + dir + " " + type + " " + name + ";\n";
    };
    line("vec2", "vUv0");
    line("vec2", "vUv1");
    line("vec4", "vTexClamp");
    line("vec4", "vColor0");
    line("vec4", "vColor1");
    line("vec4", "vColor2");
    line("vec4", "vColor3");
    line("vec2", "vFog");
    line("vec3", "vNrmView");
    return s;
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
        default: return "?";
    }
}

std::string BuildVertexSource(Variant v) {
    std::string s = "#version 450\n";
    s += VertexAttribs();
    s += Varyings("out");
    s += "layout(set=1, binding=0, std140) uniform UnifiedXform {\n" + std::string(kXformUboBody) + "} xf;\n";
    s += "layout(set=1, binding=1, std140) uniform UnifiedBones { mat4 uBones[" +
         std::to_string(SOH3D_GL_MAX_BONES) + "]; } bones;\n";
    s += "layout(set=3, binding=0, std140) uniform UnifiedMaterialUBO {\n" + std::string(kMaterialUboBody) +
         "} mat_;\n";
    s +=
        "void main() {\n"
        "    vec3 sp = aPos;\n"
        "    vec3 nM = aNrm;\n"
        // 3DS GPU 4-bone blend (the plan's non-goal: this mechanism itself stays as-is, only the
        // vertex format/shader it feeds is unified). N64 content sets uParams1.z=0 and writes
        // identity bone data, so this branch is simply skipped.
        "    if (mat_.uParams1.z > 0.5) {\n"
        "        vec4 acc = vec4(0.0); vec3 nAcc = vec3(0.0);\n"
        "        for (int i = 0; i < 4; i++) {\n"
        "            acc += aBoneW[i] * (bones.uBones[int(aBoneId[i])] * vec4(aPos, 1.0));\n"
        "            nAcc += aBoneW[i] * (mat3(bones.uBones[int(aBoneId[i])]) * aNrm);\n"
        "        }\n"
        "        sp = acc.xyz;\n"
        "        nM = nAcc;\n"
        "    }\n"
        "    gl_Position = xf.uMvp * vec4(sp, 1.0);\n"
        "    vNrmView = mat3(xf.uMv) * nM;\n"
        "    vUv0 = aUv0;\n"
        "    vUv1 = aUv1;\n"
        "    vTexClamp = aTexClamp;\n"
        "    vColor0 = aColor0;\n"
        "    vColor1 = aColor1;\n"
        "    vColor2 = aColor2;\n"
        "    vColor3 = aColor3;\n"
        "    vFog = aFog;\n"
        // lightingMode 2 (3DS scene vertex-lit): baked here per-vertex, matching the existing
        // per-vertex NdotL approach (docs/oot3d_world_lighting_re.md), not in the fragment stage.
        "    if (mat_.uParams0.y > 1.5) {\n"
        "        float ndotl = max(dot(normalize(nM), normalize(xf.uLightDir.xyz)), 0.0);\n"
        "        vec3 lit = mat_.uMatAmbient.xyz + mat_.uMatDiffuse.xyz * ndotl;\n"
        "        vColor0 = vec4(clamp(vColor0.rgb * lit, 0.0, 1.0), vColor0.a);\n"
        "    }\n"
        "}\n";
    return s;
}

std::string BuildFragmentSource(Variant v) {
    VariantFeatures f = FeaturesFor(v);
    std::string s = "#version 450\n";
    s += Varyings("in");
    s += "layout(location=0) out vec4 fragColor;\n";
    int samplerBind = 0;
    if (f.hasTex0)
        s += "layout(set=2, binding=" + std::to_string(samplerBind++) + ") uniform sampler2D uTex0;\n";
    if (f.hasTex1)
        s += "layout(set=2, binding=" + std::to_string(samplerBind++) + ") uniform sampler2D uTex1;\n";
    s += "layout(set=1, binding=0, std140) uniform UnifiedXform {\n" + std::string(kXformUboBody) + "} xf;\n";
    s += "layout(set=3, binding=0, std140) uniform UnifiedMaterialUBO {\n" + std::string(kMaterialUboBody) +
         "} mat_;\n";

    // SHADER_* operand codes (interpreter.h) — kept in sync manually; combMux is populated with
    // these exact values so N64's CCFeatures.c[2][2][4] can be copied in verbatim in Phase 3.
    s +=
        "const int SHADER_0 = 0;\n"
        "const int SHADER_INPUT_1 = 1;\n"
        "const int SHADER_INPUT_2 = 2;\n"
        "const int SHADER_INPUT_3 = 3;\n"
        "const int SHADER_INPUT_4 = 4;\n"
        "const int SHADER_TEXEL0 = 8;\n"
        "const int SHADER_TEXEL0A = 9;\n"
        "const int SHADER_TEXEL1 = 10;\n"
        "const int SHADER_TEXEL1A = 11;\n"
        "const int SHADER_1 = 12;\n"
        "const int SHADER_COMBINED = 13;\n"
        "const int SHADER_NOISE = 14;\n";

    s +=
        "float random(in vec3 value) {\n"
        "    float r = dot(sin(value), vec3(12.9898, 78.233, 37.719));\n"
        "    return fract(sin(r) * 143758.5453);\n"
        "}\n";

    // One generic evaluator for both cycles/both RGB+alpha — replaces the old per-permutation
    // sg_shader_item_to_str/sg_append_formula text generation with a runtime switch. "single" /
    // "multiply" / "mix" are not distinct formulas (see unified_material.h) — this is always the
    // fully general (A-B)*C+D, so there is exactly one code path regardless of combiner shape.
    s +=
        "vec4 texel0() {\n"
        + std::string(f.hasTex0 ? "    return texture(uTex0, clamp(vUv0, 0.5 / vec2(textureSize(uTex0, 0)), "
                                   "vTexClamp.xy));\n"
                                 : "    return vec4(1.0);\n") +
        "}\n"
        "vec4 texel1() {\n"
        + std::string(f.hasTex1 ? "    return texture(uTex1, clamp(vUv1, 0.5 / vec2(textureSize(uTex1, 0)), "
                                   "vTexClamp.zw));\n"
                                 : "    return vec4(1.0);\n") +
        "}\n"
        "vec4 evalInput(int code, vec4 combined) {\n"
        "    if (code == SHADER_0) return vec4(0.0);\n"
        "    if (code == SHADER_1) return vec4(1.0);\n"
        "    if (code == SHADER_INPUT_1) return vColor0;\n"
        "    if (code == SHADER_INPUT_2) return vColor1;\n"
        "    if (code == SHADER_INPUT_3) return vColor2;\n"
        "    if (code == SHADER_INPUT_4) return vColor3;\n"
        "    if (code == SHADER_TEXEL0) return texel0();\n"
        "    if (code == SHADER_TEXEL0A) return vec4(texel0().a);\n"
        "    if (code == SHADER_TEXEL1) return texel1();\n"
        "    if (code == SHADER_TEXEL1A) return vec4(texel1().a);\n"
        "    if (code == SHADER_COMBINED) return combined;\n"
        "    if (code == SHADER_NOISE) {\n"
        "        float n = (random(vec3(floor(gl_FragCoord.xy * mat_.uParams1.x), mat_.uParams0.w)) + 1.0) / "
        "2.0;\n"
        "        return vec4(n);\n"
        "    }\n"
        "    return vec4(0.0);\n"
        "}\n"
        "vec4 evalCycle(int cycle, vec4 combined) {\n"
        "    vec4 a = evalInput(mat_.uCombA[cycle * 2 + 0][0], combined);\n"
        "    vec4 b = evalInput(mat_.uCombA[cycle * 2 + 0][1], combined);\n"
        "    vec4 c = evalInput(mat_.uCombA[cycle * 2 + 0][2], combined);\n"
        "    vec4 d = evalInput(mat_.uCombA[cycle * 2 + 0][3], combined);\n"
        "    vec4 aA = evalInput(mat_.uCombA[cycle * 2 + 1][0], combined);\n"
        "    vec4 bA = evalInput(mat_.uCombA[cycle * 2 + 1][1], combined);\n"
        "    vec4 cA = evalInput(mat_.uCombA[cycle * 2 + 1][2], combined);\n"
        "    vec4 dA = evalInput(mat_.uCombA[cycle * 2 + 1][3], combined);\n"
        "    vec3 rgb = (a.rgb - b.rgb) * c.rgb + d.rgb;\n"
        "    float alpha = (aA.a - bA.a) * cA.a + dA.a;\n"
        "    return vec4(rgb, alpha);\n"
        "}\n";

    s += "void main() {\n";
    s += "    vec4 texel = evalCycle(0, vec4(0.0));\n";
    s += "    if (mat_.uParams0.z > 1.5) texel = evalCycle(1, texel);\n"; // cycleCount==2

    // lightingMode 1 (3DS character half-Lambert): applied HERE, not baked into vColor0 like mode
    // 2, because the combiner's SHADER_INPUT_1 must stay the raw per-vertex tint (matching what the
    // old fixed CMB shader's `t.rgb * vColor.rgb * shade` does — shade multiplies the combiner
    // OUTPUT, it isn't itself a combiner input). lightingMode 0 (N64 passthrough) and 2 (already
    // baked per-vertex) need no fragment-side action.
    s +=
        "    if (mat_.uParams0.y > 0.5 && mat_.uParams0.y < 1.5) {\n"
        "        float hl = dot(normalize(vNrmView), normalize(xf.uLightDir.xyz)) * 0.5 + 0.5;\n"
        "        texel.rgb *= (0.55 + 0.45 * hl);\n"
        "    }\n";

    if (f.grayscale)
        s += "    { float g = dot(texel.rgb, vec3(0.299, 0.587, 0.114)); texel.rgb = vec3(g); }\n";
    if (f.alphaTest)
        s += "    if (texel.a < mat_.uParams0.x) discard;\n";
    if (f.fog)
        s += "    texel.rgb = mix(texel.rgb, mat_.uFogColor.rgb, clamp(vFog.x, 0.0, 1.0));\n";

    s += "    texel = clamp(texel, 0.0, 1.0);\n";
    s += "    fragColor = texel;\n";
    s += "}\n";
    return s;
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
