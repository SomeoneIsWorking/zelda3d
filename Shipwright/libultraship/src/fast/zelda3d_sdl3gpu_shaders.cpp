#ifdef ENABLE_SDL3GPU

#include "zelda3d_sdl3gpu_shaders.h"

#include "fast/zelda3d_model_types.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

namespace Fast::Zelda3DSdl3GpuShaders {
namespace {

// ---- GLSL -> SPIR-V (glslang; same toolchain the backend uses) ----
std::once_flag g_glslOnce;
bool CompileGlsl(EShLanguage stage, const char* src, std::vector<uint32_t>& spv) {
    std::call_once(g_glslOnce, []() { glslang::InitializeProcess(); });
    glslang::TShader shader(stage);
    shader.setStrings(&src, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    // glslang defines EShMessages as a bit-mask enum, so combined flags are valid even though they
    // are not listed as a standalone enumerator.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    EShMessages msg = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, false, msg)) {
        fprintf(stderr, "[Zelda3D_SG] shader parse failed: %s\n", shader.getInfoLog());
        return false;
    }
    glslang::TProgram prog;
    prog.addShader(&shader);
    if (!prog.link(msg)) {
        fprintf(stderr, "[Zelda3D_SG] shader link failed: %s\n", prog.getInfoLog());
        return false;
    }
    glslang::SpvOptions opt;
    opt.disableOptimizer = true;
    glslang::GlslangToSpv(*prog.getIntermediate(stage), spv, &opt);
    return !spv.empty();
}

// The model UBO, declared once and shared by both stages. SDL3 GPU SPIR-V requires vertex uniform
// buffers in descriptor set 1 and fragment uniform buffers in set 3, with fragment samplers in set
// 2. The body below is byte-identical to zelda3d_vk.cpp's kVert/kFrag; only the set= decorations
// differ (Vulkan put everything in set 0).
// Stringify ZELDA3D_GL_MAX_BONES so the GLSL `uBones[N]` array size has a SINGLE source of truth
// (the macro in zelda3d_model_types.h) shared by the shader, the C++ SgUbo struct, and the upload loops below.
#define SG_STR2(x) #x
#define SG_STR(x) SG_STR2(x)
// The UBO is pushed in TWO blocks because SDL3 GPU's Vulkan backend binds each pushed uniform block
// with a descriptor range capped at MAX_UBO_SECTION_SIZE = 4096 bytes (SDL_gpu_vulkan.c): any field
// past offset 4096 reads OUTSIDE the bound range -> 0. The 64-bone array alone is 4096 bytes, so a
// single combined block (4416 B) silently zeroed uLightDir/uParams/uTintSkin/... -> black scene +
// T-posed (skin-enable lives in uTintSkin.w). SG_UBO_COMMON_BODY (the small per-draw state, ~320 B)
// is bound at binding 0 for both stages; the bone matrices go in their own block at vertex binding 1.
#define SG_UBO_COMMON_BODY       \
    "    mat4 uMP;\n"            \
    "    mat4 uMV;\n"            \
    "    vec4 uLightDir;\n"      \
    "    vec4 uParams;\n"        \
    "    vec4 uTintSkin;\n"      \
    "    vec4 uExtra;\n"         \
    "    mat4 uLightVP;\n"       \
    "    vec4 uShadow;\n"        \
    "    vec4 uFog;\n"           \
    "    vec4 uFog2;\n"          \
    "    vec4 uAmbient;\n"       \
    "    vec4 uMatDiffuse;\n"    \
    "    vec4 uPrimaryCtl;\n"    \
    "    vec4 uMatConst;\n"      \
    "    vec4 uSheen;\n"         \
    "    vec4 uTex0Xf;\n"        \
    "    vec4 uTex1Xf;\n"        \
    "    vec4 uFog3d0;\n"        \
    "    vec4 uFog3d1;\n"        \
    "    vec4 uSphNrm0;\n"       \
    "    vec4 uSphNrm1;\n"       \
    "    vec4 uSphNrm2;\n"       \
    "    vec4 uLitDif1;\n"       \
    "    vec4 uLitDif2;\n"       \
    "    vec4 uLightDir2;\n"     \
    "    uvec4 uTevStages[6];\n" \
    "    uvec4 uTevConst[2];\n"  \
    "    vec4 uTex2Xf;\n"        \
    "    vec4 uTevCtl;\n"        \
    "    vec4 uDebug;\n"
#define SG_UBO_BONES_BODY "    mat4 uBones[" SG_STR(ZELDA3D_GL_MAX_BONES) "];\n"

// The varyings, declared ONCE. `{{ q }}` renders as `out` for the vertex stage and `in` for the
// fragment stage; previously both lists were written by hand in both shaders, so a location added
// on one side only was a silent drift hazard.
constexpr const char* kVaryings = R"GLSL(layout(location=0) {{ q }} vec2 vUv;
layout(location=1) {{ q }} vec4 vColor;
layout(location=2) {{ q }} vec3 vNrmView;
layout(location=3) {{ q }} vec3 vWorld;
layout(location=4) {{ q }} float vFogDist;
layout(location=5) {{ q }} vec2 vUv1;
layout(location=6) {{ q }} vec2 vUv2;
    // PRIMARY_COLOR (PICA output register o1) — computed and SATURATED PER VERTEX, then
    // interpolated. See the kFrag comment at the `vtxLit` branch for why this cannot live in
    // the fragment shader.
layout(location=7) {{ q }} vec4 vPrim;
)GLSL";

// Vertex shader template. `{{ ubo_body }}` / `{{ ubo_bones_body }}` splice the
// SG_UBO_*_BODY macros so the UBO layout keeps its single source of truth shared with the C++ struct.
constexpr const char* kVertTemplate = R"GLSL(#version 450
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec2 aUv;
layout(location=3) in vec4 aBoneId;
layout(location=4) in vec4 aBoneW;
layout(location=5) in vec4 aColor;
layout(location=6) in vec2 aUv1;
layout(location=7) in vec2 aUv2;
{{ varyings }}
layout(set=1, binding=0, std140) uniform UBO {
{{ ubo_body }}
} ubo;
layout(set=1, binding=1, std140) uniform UBOBones {
{{ ubo_bones_body }}
} bones;
void main() {
    vColor = aColor;
    vec3 sp, nM;
    if (ubo.uTintSkin.w > 0.5) {
        vec4 acc = vec4(0.0); nM = vec3(0.0);
        for (int i = 0; i < 4; i++) {
            acc += aBoneW[i] * (bones.uBones[int(aBoneId[i])] * vec4(aPos, 1.0));
            nM  += aBoneW[i] * (mat3(bones.uBones[int(aBoneId[i])]) * aNrm);
        }
        sp = acc.xyz;
    } else { sp = aPos; nM = aNrm; }
    vec4 c = ubo.uMP * vec4(sp, 1.0);
    // N64 fog ramp input: this vertex's NDC z/w. The 3DS PICA fog path (uFog.w == 2.0) does NOT
    // use vFogDist — it derives the 3DS z-buffer depth per FRAGMENT from vWorld in kFrag.
    // FALSIFIED APPROACHES for the 3DS path, do not retry (2026-07-22, Kokiri far-band residual):
    //  - per-VERTEX depth a - b/d in vFogDist with default (perspective-correct) interpolation:
    //    z/w is screen-affine, not world-affine, so mid-triangle values undershoot -> measurably
    //    WEAKER fog than the 3DS on large distant triangles (~5-8/255 dark in the far band);
    //  - the same with noperspective interpolation: exact for on-screen vertices, but a vertex
    //    BEHIND the camera hits the max(d,1e-3) clamp and carries a huge negative depth; near-plane
    //    clipping lerps that garbage into visible near triangles (Kokiri: pale fog wedge under
    //    Link, near band +46/255). Per-fragment evaluation has neither failure mode: interpolated
    //    vWorld is world-affine (exact), and post-clipping fragments are never behind the camera.
    vFogDist = c.z / c.w;
    c.y *= ubo.uParams.x;
    c.z = (c.z + c.w) * 0.5;
    if (ubo.uLightDir.w > 0.5) c.z = c.w;
    gl_Position = c;
    vNrmView = mat3(ubo.uMV) * nM;
    // --- PICA output register o1 (PRIMARY_COLOR), evaluated PER VERTEX ------------------------
    // GROUND TRUTH (Azahar src/video_core/pica/output_vertex.cpp, OutputVertex ctor):
    //   "The hardware takes the absolute and saturates vertex colors, *before* doing
    //    interpolation"  ->  color[i] = min(|o1[i]|, 1.0)
    // So the 3DS evaluates the CmbVShader lighting term at each VERTEX, saturates the RESULT,
    // and the rasterizer interpolates the already-clamped value. Evaluating the same expression
    // per FRAGMENT on an interpolated vColor is NOT equivalent: min() is concave, so
    //   lerp(min(a,1), min(b,1))  <=  min(lerp(a,b), 1)
    // and the per-fragment form is systematically BRIGHTER across every edge where one end
    // saturates. At Kokiri noon the light sum is 1.42, so any vertex with baked colour above
    // ~0.70 clamps — and the deficit is hue-shaped, because blue's light term (1.255) clamps at
    // a higher vColor than red/green's (1.420). That is exactly the measured d8 signature
    // (+24%/+23% on R/G, +8% on B). Do not move this back into the fragment shader.
    vec3 nV = normalize(vNrmView);
    if (ubo.uAmbient.w > 0.0) {
        vec3 lit = ubo.uAmbient.xyz * ubo.uAmbient.w
                 + ubo.uLitDif1.rgb * max(dot(nV, -ubo.uLightDir.xyz), 0.0)
                 + ubo.uLitDif2.rgb * max(dot(nV, -ubo.uLightDir2.xyz), 0.0);
        vPrim = min(abs(vec4(lit * aColor.rgb, aColor.a)), vec4(1.0));
    } else if (ubo.uPrimaryCtl.x > 0.5) {
        // CmbVShader HasColor=1 overwrites the unlit c8 MatDiffuseColor seed with aColor.
        vPrim = min(abs(aColor), vec4(1.0));
    } else {
        // CmbVShader HasColor=0 leaves the authored c8 MatDiffuseColor in PRIMARY.
        vPrim = min(abs(ubo.uMatDiffuse), vec4(1.0));
    }
    vWorld = (ubo.uMV * vec4(sp, 1.0)).xyz;
    // Sphere-map normal space is the CmbVShader's c4-c6 uModelView transform. For ordinary scene
    // draws host uMV carries the same transform. Native composition may differ, so an exact
    // oracle-derived matrix can be transported independently (title wordmark: identity).
    vec3 ns = (ubo.uSphNrm0.w > 0.5)
        ? vec3(dot(ubo.uSphNrm0.xyz, nM), dot(ubo.uSphNrm1.xyz, nM), dot(ubo.uSphNrm2.xyz, nM))
        : (mat3(ubo.uMV) * nM);
    // Coordinator 0 is independent of TEX1. Wordmark mats 10/11 use CameraSphereEnvMap on TEX0
    // while leaving texture unit 1 disabled; treating that mapping as a dual-texture signal was
    // the old synthetic mode-4 bug.
    if (ubo.uSheen.w > 2.5 && ubo.uSheen.w < 3.5) {
        vec3 nv0 = normalize(ns);
        vec2 suv0 = vec2((nv0.x * 0.5 + 0.5 - ubo.uTex0Xf.z) * ubo.uTex0Xf.x,
                         (nv0.y * 0.5 + 0.5 - ubo.uTex0Xf.w) * ubo.uTex0Xf.y);
        vUv = vec2(suv0.x, 1.0 - suv0.y);
    } else {
        vUv = vec2(aUv.x + ubo.uExtra.y, 1.0 - aUv.y + ubo.uExtra.z);
    }
    // Coordinator-1 UV for the second texture. uTevCtl.y carries its mapping method;
    // uTex1Xf carries scale/translation.
    if (ubo.uTevCtl.y > 2.5 && ubo.uTevCtl.y < 3.5) {
    // Same texture-space y-flip as the UV-coordinate path below (SoH uploads textures y-flipped
    // relative to PICA texture space, which is why the UvCoordinateMap branch samples 1-uv1.y);
    // the sphere-mapped UV must flip identically or it mirrors the sampled gradient vertically.
    // The coordinator's own scale/trans applies to the sphere-mapped UV too (noclip render.ts
    // CalcTextureCoordRaw: the sphere src is still multiplied by the coordinator texture matrix
    // before the final flip).
        vec3 nv = normalize(ns);
        vec2 suv = vec2((nv.x * 0.5 + 0.5 - ubo.uTex1Xf.z) * ubo.uTex1Xf.x,
                        (nv.y * 0.5 + 0.5 - ubo.uTex1Xf.w) * ubo.uTex1Xf.y);
        vUv1 = vec2(suv.x, 1.0 - suv.y);
    } else {
        vec2 uv1 = vec2((aUv1.x - ubo.uTex1Xf.z) * ubo.uTex1Xf.x,
                        (aUv1.y - ubo.uTex1Xf.w) * ubo.uTex1Xf.y);
        vUv1 = vec2(uv1.x, 1.0 - uv1.y);
    }
    // Coordinator-2 UV for the third texture unit (generic TEV, render.multi-stage-tev), or the
    // sphere-mapped UV when coordinator 2 uses CameraSphereEnvMap (uTevCtl.z == 3), same convention
    // as uv1. aUv2 has already been resolved from the coordinator's selected CMB attribute stream.
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
}
)GLSL";

// Fragment shader template. Besides the UBO and varyings, `{{ tap_combiner }}` and
// `{{ tap_pre_fog }}` are the FRAGDBG probe insertion points. They used to be a runtime
// fragSrc.find() on a source LINE, which silently went inert when a lighting rewrite deleted the
// line it matched -- the probe reported nothing and looked like a clean result. A named hole cannot
// drift out from under the code that fills it, and BuildShaderSource verifies the fill landed.
constexpr const char* kFragTemplate = R"GLSL(#version 450
{{ varyings }}
layout(location=0) out vec4 frag;
layout(set=3, binding=0, std140) uniform UBO {
{{ ubo_body }}
} ubo;
layout(set=2, binding=0) uniform sampler2D uTex;
    // set=2 binding=1 was the (removed) sun-shadow map's slot; it now carries the THIRD texture
    // unit (uTex2) for the generic TEV path (render.multi-stage-tev — Zora's water combines
    // tex0+tex1+tex2). Bound to the dummy texture on draws without a third binding, so the
    // 3-sampler bind layout is unchanged.
layout(set=2, binding=1) uniform sampler2D uTex2;
layout(set=2, binding=2) uniform sampler2D uTex1;
    // RE'd 3DS fog LUT node value at t = i/128 (FogResUpdater FUN_002cdbfc, mode 0 linear —
    // oot3d-decomp title_env_lighting.md §13): eyeDist = b/(a - t) (inverse projection), then
    // the linear fogNear..fogFar window. uFog3d0 = (a, b, fogNear, fogFar).
float fog3dNode(float t) {
    float d = ubo.uFog3d0.y / max(ubo.uFog3d0.x - t, 1e-6);
    if (d < ubo.uFog3d0.z) return 1.0;
    if (d > ubo.uFog3d0.w) return 0.0;
    return (ubo.uFog3d0.w - d) / (ubo.uFog3d0.w - ubo.uFog3d0.z);
}
    // --- Generic PICA TEV evaluator (render.multi-stage-tev) ---------------------------------
    // Faithful per-stage emulation of the PICA200 texture-combiner chain, driven by the packed
    // per-material words in uTevStages (packing: Zelda3DGlGroup::tevStagePack, zelda3d_model_types.h).
    // Semantics per Azahar's sw_rasterizer TevStageConfig (the same core the oracle runs):
    // each stage combines up to three modified sources with its op, scales the result x1/x2/x4,
    // and CLAMPS ON REGISTER WRITE (the same clamp-the-product rule as the lit path below).
    // Source codes: 0 Primary (the vertex-lit output color), 1 FragPrimary, 2 FragSecondary,
    // 3..5 Texture0..2, 6 Texture3 (no unit; falls back to tex0), 13 PreviousBuffer,
    // 14 Constant (per-stage slot from uTevConst), 15 Previous.
    // KNOWN APPROXIMATIONS (rare in corpus, none at Zora/Kokiri — tools/tev_corpus_survey.py):
    //  - FragPrimary/FragSecondary (fragment lighting, 199+69 materials): mapped to the same
    //    vertex-lit primary / (0,0,0,1) — fragment lighting itself is not emulated yet.
    //  - the INITIAL combiner-buffer color (PICA tev_combiner_buffer_color) is not parsed from
    //    the CMB and is taken as vec4(0). This is exact for every material in this ROM: all 14
    //    that read PREVBUF latch exactly one stage before the read, so the read always returns a
    //    latched stage output and never the initial value (tools/tev_corpus_survey.py reports
    //    both columns). FALSIFIER: a material that reads PREVBUF with no preceding latch.
{{ generic_tev_functions }}
void main() {
    vec4 t = texture(uTex, vUv);
    // PICA TEV stage 0 dual-texture combine (ADD_MULT: (t0 + t1) * t0, saturating the sum per
    // stage — g_title.cmb fire-glow detail mask, title_logo_fireglow_cmab.md §3.1). The alpha
    // chain does NOT include TEXTURE1 (stage-0 alpha = primary.a * t0.a), so only .rgb changes.
    // Mode 1 (g_title.cmb fire-glow): (t0+t1)*t0, all in this stage. Mode 2 (shield glint,
    // title_logo_us mat6/9): (t0+t1)*PRIMARY — leaves the *PRIMARY to the existing
    // `rgb = t.rgb * vColor.rgb` compound below, so only the ADD+clamp happens here. Mode 3
    // (sword/shield detail mask, title_logo_us mat4/5/7): scale2*(PRIMARY*t0*t1) — same
    // deferred-PRIMARY trick, this stage does scale2*t0*t1.
    if (ubo.uSheen.y > 1.5) {
        vec3 t1 = texture(uTex1, vUv1).rgb;
        if (ubo.uSheen.y > 2.5) {
            t.rgb = clamp(t.rgb * t1 * ubo.uSheen.z, 0.0, 1.0);
        } else {
            t.rgb = clamp(t.rgb + t1, 0.0, 1.0);
        }
    } else if (ubo.uSheen.y > 0.5) {
        vec3 t1 = texture(uTex1, vUv1).rgb;
        t.rgb = clamp(t.rgb + t1, 0.0, 1.0) * t.rgb;
    }
    // Generic TEV draws (uTevCtl.x > 0): PICA's alpha test compares the FINAL combiner alpha,
    // not the raw texel — their discard happens after tevRun below.
    bool tevG = (ubo.uTevCtl.x > 0.5);
    int afn = int(ubo.uTevCtl.w + 0.5);
    if (!tevG && afn > 0 && !alphaPass(t.a, ubo.uParams.z, afn - 1)) discard;
    gl_FragDepth = gl_FragCoord.z + ubo.uParams.w;
    // Flat modulator for non-vertex-lit draws: the N64-fallback scene tint or a caller-supplied
    // per-draw color. The former half-Lambert form term (0.55+0.45·hl — a synthetic, magic-constant
    // shading model) was removed with the shadow/AO enhancements: OoT3D lighting only.
    vec3 shade = ubo.uTintSkin.xyz;
    // Wordmark sheen (title_logo_actor.md §6.3/§6.6): CmbVShader's vertex-lit color term for the
    // wordmark's single enabled light, verified against the oracle's live c81/c82 uniforms:
    //   o1 = matAmb(1)*lightAmb(0.18) + max(0, dot(N, -L)) * matDif(1)*lightDif(1)
    //      = 0.18 + max(0, dot(N, -L))
    // Two load-bearing details vs the previous (falsified) additive-boost port: (a) the light
    // AMBIENT is 0.18 and the DIFFUSE is WHITE (the old 1+0.1834*ndotl had the slot colors
    // swapped — it capped the sheen swing at x1.18 while the oracle measures x1.40); (b) PICA's
    // dp3 uses -c80 (the NEGATED light dir) — with the letters' flat N=(0,0,1) and +L the dot is
    // negative and max() kills the term entirely (SoH measured bit-flat x1.000 across the ramp).
    // uSheen.x carries the 0.18 ambient (also the >0 gate). PICA clamps the vertex color to [0,1]
    // before the TEV reads it. Only ever nonzero for the title wordmark's own draws
    // (zelda3d_lighting.cpp's per-model light-direction override).
    if (ubo.uSheen.x > 0.0) {
        float ndotl = max(dot(normalize(vNrmView), -normalize(ubo.uLightDir.xyz)), 0.0);
        shade *= clamp(ubo.uSheen.x + ndotl, 0.0, 1.0);
    }
    // OoT3D CmbVShader vertex-lit path (#153/#111 — oot3d-decomp title_env_lighting.md §10):
    // for EVERY vertexLighting=1 material (scene rooms AND characters/props) the 3DS computes
    //   o1 = clamp(Σ_i matAmb·lightAmb_i + max(0, dot(N, -L_i))·matDif·lightDif_i, 0, 1) · vColor
    // then the TEV modulates the texel by that PRIMARY_COLOR (stage scale on uExtra.w below).
    // Terrain materials have matDif=BLACK so their diffuse terms vanish — this reduces exactly
    // to the previously-verified ambient-only scene path (parity-map rows unaffected). Character
    // materials (Link/Epona: matAmb=0.4, matDif=0.5) get the real directional shading. This is
    // the ONLY lighting model for CMB draws — the synthetic half-Lambert form term, the sun
    // shadow-map receive, and SSAO were removed (user directive 2026-07-16: OoT3D lighting and
    // shading only). vertexLighting=0 draws take the flat-tint fallback below (uTintSkin is the
    // N64-fallback scene tint or a caller-supplied flat modulator, not a lighting model).
    bool vtxLit = (ubo.uAmbient.w > 0.0);
    vec3 rgb;
    float outA = t.a * vColor.a;
    // PRIMARY_COLOR — the vertex-lit output color the TEV chain modulates by. Shared by the
    // legacy fast path and the generic TEV path so the lighting model has exactly ONE home.
    vec4 prim;
    if (vtxLit) {
    // Clamp order is the PRODUCT, verified by A/B vs the oracle (2026-07-22): PICA clamps o1
    // when the output register is WRITTEN — i.e. clamp(Σ·vColor) — not the light sum first.
    // clamp(Σ)·vColor was tried and measured ~30% dark on near grass (settled Kokiri noon,
    // rows 0.85-0.97: (62,71) vs oracle (90,99); this form gives (88,101)). Do not re-flip.
    // RE-CONFIRMED 2026-07-22 from ORACLE GROUND TRUTH, not an A/B fit (which the texpack
    // asymmetry could have flipped): Azahar's per-fragment probe (SOH3D_HARNESS_SW=1 +
    // SOH3D_PIXEL_TEX, `PIXEL ... primary=`) reads the 3DS's real PRIMARY_COLOR. At Kokiri the
    // scene's light sum is 2 x 0.7098 = 1.4196 (> 1), so clamp(sum) would pin o1 at vColor and
    // make PRIMARY INDEPENDENT of the ambient. It is not: at the same fragment (329,52), moving
    // dayTime 0x6000 -> 0x4000 moves amb0 (0.7098,0.7098,0.62745) -> (0.46667,0.46667,0.23922)
    // and primary (77,77,69) -> (51,51,26) — ratios 0.662/0.662/0.377 vs the ambient's
    // 0.658/0.658/0.381. PRIMARY scales linearly with the ambient, so the clamp is on the
    // PRODUCT. Our own live lit term measures (1.4218,1.4218,1.2566) = 2 x the scene ambient.
    // MOVED TO THE VERTEX SHADER 2026-07-23 (render.kokiri-near-terrain-overbright). The clamp
    // ORDER above is still correct and unchanged — what was wrong was the clamp STAGE: PICA
    // saturates o1 per VERTEX before interpolation (Azahar pica/output_vertex.cpp, hardware-
    // tested), so evaluating clamp(lit*vColor) per FRAGMENT on interpolated inputs is brighter
    // wherever a triangle straddles saturation. `vPrim` carries the per-vertex result.
        prim = vPrim;
    } else {
        // vPrim already contains the exact unlit CmbVShader choice (aColor or MatDiffuse).
        // Actor behavior modulation is an explicit host layer after that choice, not a
        // replacement for the shader's HasColor branch.
        prim = vPrim;
        if (ubo.uParams.y > 0.5) {
            prim.rgb *= shade;
        }
    }
    // Generic per-stage TEV path (render.multi-stage-tev): evaluate the material's real
    // combiner chain — multi-texture, multi-stage, per-stage ops/operands/scales/consts —
    // instead of the single-MODULATE legacy compound below. This is what "Zora's water is dark
    // and desaturated" was: tex1/tex2 + MultiplyThenAdd stages our fixed pipeline dropped.
    if (tevG) {
        vec4 t1s = texture(uTex1, vUv1);
        vec4 t2s = texture(uTex2, vUv2);
        vec4 tev = tevRun(prim, t, t1s, t2s);
        int afn2 = int(ubo.uTevCtl.w + 0.5);
        if (afn2 > 0 && !alphaPass(tev.a, ubo.uParams.z, afn2 - 1)) discard;
        rgb = tev.rgb;
        outA = tev.a;
    } else {
        rgb = t.rgb * prim.rgb;
    }
    // PICA200 CONSTANT-color modulation (EnHy townsfolk body color, title fire-glow gold
    // flicker). uMatConst.a is both the apply flag AND the CONSTANT-stage's hardware RGB scale:
    // 0 = skip (materials that don't reference CONSTANT), 1/2/4 = multiply by uMatConst.rgb then
    // by the stage scale (PICA scales each stage's output AFTER the combine — g_title.cmb's
    // stage 1 is 2.0*(PREV*CONST0), the fire-glow "half brightness" factor, fireglow doc §3.2).
    // (Legacy path only — the generic TEV path already applied each stage's real CONSTANT.)
{{ tap_combiner }}
    if (!tevG && ubo.uMatConst.a >= 0.5) rgb = clamp(rgb * ubo.uMatConst.rgb * ubo.uMatConst.a, 0.0, 1.0);
    // uAmbient.w carries the ENABLED-LIGHT COUNT for this draw (0 = ambient path inactive), not a
    // 0/1 gate: title_env_lighting.md §10/§11 disassembled OoT3D's real PICA vertex-lit program and
    // found `matAmbient*sceneAmbient` is summed once PER ENABLED light slot (2 for standard N64
    // scenes), not applied once. Every enabled slot in SoH's data model carries the identical scene
    // ambient colour (SoH tracks one ambient, not per-slot ones), so the real N-term sum reduces
    // exactly to `uAmbient.xyz * uAmbient.w` here — a real sum, not a fitted multiplier.
    // TEV stage scale (uExtra.w = the material's authored combiner RGB scale). The ambient term
    // itself moved into the vtxLit light sum above; vertex-lit characters (uParams.y > 0.5 &&
    // vtxLit) take the scale too — their stage-0 combiner is the same MODULATE ×2 as scene
    // materials (offline CMB dump, #153) that the old half-Lambert branch never applied.
    // (Legacy path only — the generic TEV path applied each stage's own hardware scale.)
    if (!tevG && (ubo.uParams.y < 0.5 || vtxLit)) {
        rgb = clamp(rgb, 0.0, 1.0) * ubo.uExtra.w;
    }
    // OoT3D PICA distance fog (uFog.w == 2.0; title port — oot3d-decomp title_env_lighting.md
    // §13). The 3DS z-buffer depth of THIS FRAGMENT is derived from the interpolated world
    // position (world-affine -> exact; see the vertex-shader note for the two falsified
    // vertex-level variants): d = eye depth along the view axis, depth = a - b/d = the
    // fragment's z/w under the 3DS projection — exactly the depth-buffer value PICA indexes
    // its fog LUT with. PICA samples a 128-entry LUT at index depth*128 and LERPs
    // value->value+diff INSIDE the entry; with the scene's compressed depth range entry 127
    // spans eye ~873..zFar, so this piecewise-linear-in-DEPTH interpolation (not the
    // underlying distance curve) is the visible haze. fog3dNode() is the RE'd FogResUpdater
    // node value (linear mode 0): eyeDist(t) = b/(a-t), then the fogNear/fogFar linear
    // window. (The 3DS's 11/13-bit LUT quantization is omitted: <=1/2048 in the factor,
    // sub-LSB of the 8-bit output.) Never applied to sky (uLightDir.w).
    // FALSIFIED (2026-07-22): this fog was a suspect for Kokiri's near-terrain +18%. REPL `fog3d 0`
    // (the gZelda3dFog3dForceOff latch) moves draw d8 only 1.184 -> 1.180, while it correctly drives
    // the FAR draws hard (d9 1.033 -> 0.799, d7 0.979 -> 0.617). Our window is byte-identical to the
    // oracle's anyway (a=1.000584 b=7.0041 near=800 far=2400, colour (244,239,130) — SG_DUMP vs the
    // oracle's live LUT lutS=(1,1,1,0.979)). The near-terrain residual is upstream of fog.
{{ tap_pre_fog }}
    if (ubo.uFog.w > 1.5 && ubo.uLightDir.w < 0.5) {
        float d3 = dot(vWorld, ubo.uFog3d1.xyz) - ubo.uFog3d1.w;
        float depth3ds = ubo.uFog3d0.x - ubo.uFog3d0.y / max(d3, 1e-3);
        float x = clamp(depth3ds, 0.0, 1.0) * 128.0;
        float i0 = min(floor(x), 127.0);
        float f0 = fog3dNode(i0 * (1.0 / 128.0));
        float f1 = fog3dNode((i0 + 1.0) * (1.0 / 128.0));
        float factor = clamp(f0 + (f1 - f0) * (x - i0), 0.0, 1.0);
        rgb = mix(ubo.uFog.xyz, rgb, factor);
    } else if (ubo.uFog.w > 0.5 && ubo.uLightDir.w < 0.5) {
        float f = clamp(vFogDist * ubo.uFog2.x + ubo.uFog2.y, 0.0, 255.0) * (1.0 / 255.0);
        rgb = mix(rgb, ubo.uFog.xyz, f);
    }
    frag = vec4(rgb, outA * ubo.uExtra.x);
}
)GLSL";

constexpr const char* kOverlayDepthFrag = "#version 450\n"
                                          "void main() {\n"
                                          "    gl_FragDepth = 1.0;\n"
                                          "}\n";

bool ReplaceToken(std::string& source, const char* token, const char* value, std::string& error) {
    const std::size_t position = source.find(token);
    if (position == std::string::npos) {
        error = std::string("shader template is missing token ") + token;
        return false;
    }
    source.replace(position, strlen(token), value);
    return true;
}

bool ReplaceAllTokens(std::string& source, const char* token, const char* value, std::string& error) {
    std::size_t position = 0;
    bool replaced = false;
    while ((position = source.find(token, position)) != std::string::npos) {
        source.replace(position, strlen(token), value);
        position += strlen(value);
        replaced = true;
    }
    if (!replaced) {
        error = std::string("shader template is missing token ") + token;
    }
    return replaced;
}

bool HasUnresolvedToken(const std::string& source, std::string& error) {
    const std::size_t position = source.find("{{");
    if (position == std::string::npos) {
        return false;
    }
    const std::size_t end = source.find("}}", position + 2);
    const std::string token =
        end == std::string::npos ? source.substr(position, 32) : source.substr(position, end - position + 2);
    error = "shader template has unresolved token " + token;
    return true;
}

} // namespace

bool Compile(EShLanguage stage, const char* source, std::vector<uint32_t>& spirv) {
    return CompileGlsl(stage, source, spirv);
}

bool BuildSources(const char* genericTevFunctions, const char* tapCombiner, const char* tapPreFog,
                  std::string& vertexSource, std::string& fragmentSource, std::string& error) {
    std::string vertexVaryings = kVaryings;
    std::string fragmentVaryings = kVaryings;
    if (!ReplaceAllTokens(vertexVaryings, "{{ q }}", "out", error) ||
        !ReplaceAllTokens(fragmentVaryings, "{{ q }}", "in", error)) {
        return false;
    }

    vertexSource = kVertTemplate;
    if (!ReplaceToken(vertexSource, "{{ varyings }}", vertexVaryings.c_str(), error) ||
        !ReplaceToken(vertexSource, "{{ ubo_body }}", SG_UBO_COMMON_BODY, error) ||
        !ReplaceToken(vertexSource, "{{ ubo_bones_body }}", SG_UBO_BONES_BODY, error)) {
        return false;
    }

    fragmentSource = kFragTemplate;
    if (!ReplaceToken(fragmentSource, "{{ varyings }}", fragmentVaryings.c_str(), error) ||
        !ReplaceToken(fragmentSource, "{{ ubo_body }}", SG_UBO_COMMON_BODY, error) ||
        !ReplaceToken(fragmentSource, "{{ generic_tev_functions }}", genericTevFunctions, error) ||
        !ReplaceToken(fragmentSource, "{{ tap_combiner }}", tapCombiner, error) ||
        !ReplaceToken(fragmentSource, "{{ tap_pre_fog }}", tapPreFog, error)) {
        return false;
    }
    return !HasUnresolvedToken(vertexSource, error) && !HasUnresolvedToken(fragmentSource, error);
}

const char* OverlayDepthFragment() {
    return kOverlayDepthFrag;
}

} // namespace Fast::Zelda3DSdl3GpuShaders

#endif
