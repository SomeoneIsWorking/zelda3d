// Zelda3D SDL3 GPU render path. See include/fast/zelda3d_sdl3gpu.h for the role split with zelda3d_gl.cpp.
//
// Renders the collected OoT3D draw items (textured / GPU-skinned / half-Lambert-lit, per-group
// blend + depth-write + alpha-test + decal depth-bias + mesh_id visibility) as OPS appended into
// the SDL3 GPU backend's deferred op-list, so the 3DS content replays in the SAME single render
// pass as the N64 geometry (depth-correct interleave, NO separate-pass handshake). This is the
// unified-renderer model (user directive 2026-06-26). The model shaders/UBO are the Vulkan path's
// (zelda3d_vk.cpp) verbatim, retargeted to SDL3 GPU's SPIR-V resource-set binding model.
#ifdef ENABLE_SDL3GPU

#include "fast/zelda3d_sdl3gpu.h"
#include "fast/backends/gfx_sdl3gpu.h"
#include "fast/backends/zelda3d_sdl3gpu.h" // Fast::Zelda3DRenderer (this module's state, folded into the backend)

// inja — the GLSL shaders below are inja templates (see the CMake comment for why not fmt).
#include <inja/inja.hpp>

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <vector>
#include <map>
#include <unordered_map>
#include <array>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <mutex>

#include "fast/zelda3d_sg_ubo.h" // SgUbo layout + push-block size invariants (single source of truth)
#include "fast/unified_vtx.h"      // render-unification (kanban #131), Phase 2
#include "fast/unified_material.h"
#include "fast/unified_ubo.h"

using Fast::GfxRenderingAPISdl3Gpu;
using Fast::g_activeSdl3GpuApi;
// The renderer's record types + class (defined in fast/backends/zelda3d_sdl3gpu.h) — brought into this
// scope so the out-of-class method definitions below can name them unqualified in their signatures.
using Fast::GeomRec;
using Fast::PipeKey;
using Fast::SgGroup;
using Fast::SgModel;
using Fast::Zelda3DRenderer;
using Zelda3DSg::SgUbo;
constexpr uint32_t kSgCommonBytes = Zelda3DSg::kCommonBytes;
constexpr uint32_t kSgBonesBytes = Zelda3DSg::kBonesBytes;

// Title wordmark sheen (title_logo_actor.md §6.3/§6.6): the light-env slot's AMBIENT color is
// (0.18,0.18,0.18,1) and its DIFFUSE color is WHITE (1,1,1,1) — byte-exact from the literal pool
// at 0x004d9924 in code.bin AND read back live from CmbVShader's c81/c82 uniforms at the wordmark
// draw (oracle vsuni log, 2026-07-10: dif0=(1,1,1,1), amb0=(0.18,0.18,0.18,1), one enabled
// light). The per-vertex expression is therefore shade = 0.18 + max(0, dot(N, -L(t))) — the
// DIRECTIONAL term is the dominant one (not a small additive bonus on an ambient of 1; the old
// 0.1834 "diffuse" constant here had the two slot colors swapped). Only the direction L(t) is
// animated per-frame by the caller (Zelda3D_GL_SetLightDirOverride).
constexpr float kWordmarkLightAmbient = 0.18f;

// ---- Shared scene/light/effect globals (owned by zelda3d_gl.cpp, set per frame by zelda3d.c) ----
extern "C" float gZelda3dLightDirWorld[3];
extern "C" int gZelda3dFaceCull;
extern "C" int gZelda3dFaceCullFlip;
extern "C" int gZelda3dFogEnable;
extern "C" float gZelda3dFogColor[3];
extern "C" float gZelda3dFogMul;
extern "C" float gZelda3dFogOffset;
extern "C" int gZelda3dFog3dOn;     // OoT3D PICA distance fog (title port) — zelda3d_gl.cpp
extern "C" float gZelda3dFog3d[8];  // { a, b, fogNear, fogFar, fwd.xyz, dot(fwd, eye) }
extern "C" float gZelda3dWorldAmbColor[3];
extern "C" float gZelda3dWorldAmb;
extern "C" int gZelda3dWorldLit;
// Live scene light params fed by z_kankyo via Zelda3D_GL_SetLightParams (zelda3d_gl.cpp).
// Used at UBO fill time to pre-bake scene-modulated matAmbient / matDiffuse for the unified
// vertex-lit shader so it matches OoT3D's real formula sceneAmb*matAmb + sceneDif*matDif*NdotL.
extern "C" float gZelda3dAmbient[3];
extern "C" float gZelda3dLight1Col[3];
extern "C" float gZelda3dLight2Dir[3];
extern "C" float gZelda3dLight2Col[3];
// Enabled-light count for the real per-enabled-light ambient sum (see uAmbient.w fill/consumer
// comments below; zelda3d_gl.cpp, set from live envCtx.lightSettings by zelda3d.c).
extern "C" float gZelda3dAmbientLightCount;
extern "C" int gUnifiedRenderer; // render-unification effort (kanban #131): bit 0 = CMB unified
// REPL `sgdump <modelId>`: arm a one-shot per-group render-state dump for the next draw of that model.
extern "C" int g_sgDumpModel = -1;
// ZELDA3D_SG_DUMPTEX=<modelId|all>: at that model's upload, log each source texture's mean RGBA (catches a
// black/failed texture decode, which uploads happen at scene-load before the REPL can arm sgdump).
// "all" dumps every model's textures (for finding the right modelId without a prior run).
static int g_sgDumpTexModel = []() {
    const char* v = getenv("ZELDA3D_SG_DUMPTEX");
    if (!v) return -1;
    if (strcmp(v, "all") == 0) return -2;  // sentinel: dump all
    return atoi(v);
}();
static bool g_sgDumpTexAll = (g_sgDumpTexModel == -2);
static int g_sgDumpTexActual = g_sgDumpTexAll ? -1 : g_sgDumpTexModel;
extern "C" int gZelda3dHlGroup;

// DRAW ISOLATION — our counterpart of the oracle harness's `drawskip`, and the reason it exists:
// ZELDA3D_SG_FRAGDBG isolates a COMBINER STAGE but applies it to every draw, so its readback is a
// whole-frame composite. That is fine when the draw you care about owns its pixels, and useless when
// it does not: Zora's water layer d9 has ZERO pixels that no other draw also covers, so neither
// `tev_mask_ratio --exclusive` nor a FRAGDBG frame can attribute anything to it, and the oracle's
// per-fragment `PIXEL ...` probe has no like-for-like counterpart on our side (instrument I002).
// Rendering ONE group and nothing else closes that gap: the resulting frame IS that draw's own
// output, so a FRAGDBG mode over it measures the same quantity the oracle's probe reports.
//
// `sgdrawonly <n>` (REPL) / ZELDA3D_SG_DRAWONLY=<n> renders only the n-th Zelda3D group of the frame;
// `sgdrawlist` (REPL) / ZELDA3D_SG_DRAWLIST=1 dumps one frame's group list so you can find the n you
// want. Indices are per-frame and sequential in append order — the same order that matters for
// translucency — so they are stable for a frozen camera and meaningless across a moving one.
extern "C" int gZelda3dSgDrawOnly; // -1 = draw everything (default)
extern "C" int gZelda3dSgDrawList; // 1 = dump this frame's group list, then self-clears
static int g_sgDrawIdx = 0;        // groups appended so far this frame
// strength/bias the same way the Vulkan path does.

static int sgFaceCullOn() {
    if (gZelda3dFaceCull < 0) {
        const char* e = getenv("ZELDA3D_FACECULL");
        gZelda3dFaceCull = (e && e[0] == '0') ? 0 : 1; // default ON
    }
    return gZelda3dFaceCull;
}

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
    EShMessages msg = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
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
// (the macro in zelda3d_gl.h) shared by the shader, the C++ SgUbo struct, and the upload loops below.
#define SG_STR2(x) #x
#define SG_STR(x) SG_STR2(x)
// The UBO is pushed in TWO blocks because SDL3 GPU's Vulkan backend binds each pushed uniform block
// with a descriptor range capped at MAX_UBO_SECTION_SIZE = 4096 bytes (SDL_gpu_vulkan.c): any field
// past offset 4096 reads OUTSIDE the bound range -> 0. The 64-bone array alone is 4096 bytes, so a
// single combined block (4416 B) silently zeroed uLightDir/uParams/uTintSkin/... -> black scene +
// T-posed (skin-enable lives in uTintSkin.w). SG_UBO_COMMON_BODY (the small per-draw state, ~320 B)
// is bound at binding 0 for both stages; the bone matrices go in their own block at vertex binding 1.
#define SG_UBO_COMMON_BODY \
    "    mat4 uMP;\n" \
    "    mat4 uMV;\n" \
    "    vec4 uLightDir;\n" \
    "    vec4 uParams;\n" \
    "    vec4 uTintSkin;\n" \
    "    vec4 uExtra;\n" \
    "    mat4 uLightVP;\n" \
    "    vec4 uShadow;\n" \
    "    vec4 uFog;\n" \
    "    vec4 uFog2;\n" \
    "    vec4 uAmbient;\n" \
    "    vec4 uMatConst;\n" \
    "    vec4 uSheen;\n" \
    "    vec4 uTex1Xf;\n" \
    "    vec4 uFog3d0;\n" \
    "    vec4 uFog3d1;\n" \
    "    vec4 uSphRot0;\n" \
    "    vec4 uSphRot1;\n" \
    "    vec4 uSphRot2;\n" \
    "    vec4 uLitDif1;\n" \
    "    vec4 uLitDif2;\n" \
    "    vec4 uLightDir2;\n" \
    "    uvec4 uTevStages[6];\n" \
    "    uvec4 uTevConst[2];\n" \
    "    vec4 uTex2Xf;\n" \
    "    vec4 uTevCtl;\n"
#define SG_UBO_BONES_BODY \
    "    mat4 uBones[" SG_STR(ZELDA3D_GL_MAX_BONES) "];\n"

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

// Vertex shader, as an inja template. `{{ ubo_body }}` / `{{ ubo_bones_body }}` splice the
// SG_UBO_*_BODY macros so the UBO layout keeps its single source of truth shared with the C++ struct.
constexpr const char* kVertTemplate = R"GLSL(#version 450
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec2 aUv;
layout(location=3) in vec4 aBoneId;
layout(location=4) in vec4 aBoneW;
layout(location=5) in vec4 aColor;
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
    } else {
        vPrim = min(abs(aColor), vec4(1.0));
    }
    vWorld = (ubo.uMV * vec4(sp, 1.0)).xyz;
    vUv = vec2(aUv.x + ubo.uExtra.y, 1.0 - aUv.y + ubo.uExtra.z);
    // Coordinator-1 UV for the dual-texture detail mask. uSheen.w carries the coordinator-1
    // mapping method (noclip TextureCoordinatorMappingMethod): 1=UvCoordinateMap (scale*(uv-trans)),
    // 3=CameraSphereEnvMap (normal-derived UV: reflect the view vector about the normal, map to
    // [0,1]²). The wordmark's decorations (mat4-9) use sphere mapping — for the flat (0,0,1) normals
    // this samples the texture's center, which is bright, not a per-vertex UV location (which was
    // dim, making the multiply combiner produce near-black output). uTex1Xf: .xy=scale, .zw=trans.
    // Sphere-map normal space: view space. For scene draws mat3(uMV) approximates it; for the
    // title 2D ortho-overlay pass uMV is a fixed screen placement carrying NO camera at all, so
    // the caller supplies the live camera's view-rotation rows in uSphRot0..2 (uSphRot0.w = gate)
    // and the normal is rotated into REAL view space — matching the 3DS, where these decorations
    // are composited through the live cs-camera's view matrix (title_logo_actor.md §6.1).
    vec3 ns = (ubo.uSphRot0.w > 0.5)
        ? vec3(dot(ubo.uSphRot0.xyz, nM), dot(ubo.uSphRot1.xyz, nM), dot(ubo.uSphRot2.xyz, nM))
        : (mat3(ubo.uMV) * nM);
    if (ubo.uSheen.w > 2.5) {
    // Same texture-space y-flip as the UV-coordinate path below (SoH uploads textures y-flipped
    // relative to PICA texture space, which is why the UvCoordinateMap branch samples 1-uv1.y);
    // the sphere-mapped UV must flip identically or it mirrors the sampled gradient vertically.
    // The coordinator's own scale/trans applies to the sphere-mapped UV too (noclip render.ts
    // CalcTextureCoordRaw: the sphere src is still multiplied by the coordinator texture matrix
    // before the final flip) — identity for every wordmark decoration except mat4 (scaleT=2).
        vec3 nv = normalize(ns);
        vec2 suv = vec2((nv.x * 0.5 + 0.5 - ubo.uTex1Xf.z) * ubo.uTex1Xf.x,
                        (nv.y * 0.5 + 0.5 - ubo.uTex1Xf.w) * ubo.uTex1Xf.y);
        vUv1 = vec2(suv.x, 1.0 - suv.y);
    } else {
        vec2 uv1 = vec2((aUv.x - ubo.uTex1Xf.z) * ubo.uTex1Xf.x, (aUv.y - ubo.uTex1Xf.w) * ubo.uTex1Xf.y);
        vUv1 = vec2(uv1.x, 1.0 - uv1.y);
    }
    // Coordinator-2 UV for the third texture unit (generic TEV, render.multi-stage-tev). Every
    // OoT3D coordinator sources texcoord0 (corpus-verified, tools/tev_corpus_survey.py), so uv2
    // is the same baked UV through coordinator 2's scale/translate — or the sphere-mapped UV
    // when coordinator 2 uses CameraSphereEnvMap (uTevCtl.z == 3), same convention as uv1.
    if (ubo.uTevCtl.z > 2.5 && ubo.uTevCtl.z < 3.5) {
        vec3 nv2 = normalize(ns);
        vec2 suv2 = vec2((nv2.x * 0.5 + 0.5 - ubo.uTex2Xf.z) * ubo.uTex2Xf.x,
                         (nv2.y * 0.5 + 0.5 - ubo.uTex2Xf.w) * ubo.uTex2Xf.y);
        vUv2 = vec2(suv2.x, 1.0 - suv2.y);
    } else {
        vec2 uv2 = vec2((aUv.x - ubo.uTex2Xf.z) * ubo.uTex2Xf.x, (aUv.y - ubo.uTex2Xf.w) * ubo.uTex2Xf.y);
        vUv2 = vec2(uv2.x, 1.0 - uv2.y);
    }
}
)GLSL";

// Fragment shader, as an inja template. Besides the UBO and varyings, `{{ tap_combiner }}` and
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
    // per-material words in uTevStages (packing: Zelda3DGlGroup::tevStagePack, zelda3d_gl.h).
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
vec4 tevSrc(uint code, vec4 prim, vec4 t0, vec4 t1, vec4 t2, vec4 prev, vec4 pbuf, uint kidx) {
    if (code == 0u || code == 1u) return prim;
    if (code == 2u) return vec4(0.0, 0.0, 0.0, 1.0);
    if (code == 3u || code == 6u) return t0;
    if (code == 4u) return t1;
    if (code == 5u) return t2;
    if (code == 14u) return unpackUnorm4x8(ubo.uTevConst[kidx >> 2][kidx & 3u]);
    if (code == 15u) return prev;
    if (code == 13u) return pbuf;
    return vec4(0.0);
}
vec3 tevColorMod(uint m, vec4 s) {
    if (m == 0u) return s.rgb;
    if (m == 1u) return 1.0 - s.rgb;
    if (m == 2u) return vec3(s.a);
    if (m == 3u) return vec3(1.0 - s.a);
    if (m == 4u) return vec3(s.r);
    if (m == 5u) return vec3(1.0 - s.r);
    if (m == 8u) return vec3(s.g);
    if (m == 9u) return vec3(1.0 - s.g);
    if (m == 12u) return vec3(s.b);
    if (m == 13u) return vec3(1.0 - s.b);
    return s.rgb;
}
float tevAlphaMod(uint m, vec4 s) {
    if (m == 0u) return s.a;
    if (m == 1u) return 1.0 - s.a;
    if (m == 2u) return s.r;
    if (m == 3u) return 1.0 - s.r;
    if (m == 4u) return s.g;
    if (m == 5u) return 1.0 - s.g;
    if (m == 6u) return s.b;
    if (m == 7u) return 1.0 - s.b;
    return s.a;
}
vec3 tevColorOp(uint op, vec3 a, vec3 b, vec3 c) {
    if (op == 0u) return a;
    if (op == 1u) return a * b;
    if (op == 2u) return a + b;
    if (op == 3u) return a + b - 0.5;
    if (op == 4u) return mix(b, a, c);
    if (op == 5u) return a - b;
    if (op == 6u || op == 7u) return vec3(4.0 * dot(a - 0.5, b - 0.5));
    if (op == 8u) return a * b + c;
    return clamp(a + b, 0.0, 1.0) * c;
}
float tevAlphaOp(uint op, float a, float b, float c) {
    if (op == 0u) return a;
    if (op == 1u) return a * b;
    if (op == 2u) return a + b;
    if (op == 3u) return a + b - 0.5;
    if (op == 4u) return mix(b, a, c);
    if (op == 5u) return a - b;
    if (op == 8u) return a * b + c;
    if (op == 9u) return clamp(a + b, 0.0, 1.0) * c;
    return a;
}
vec4 tevRun(vec4 prim, vec4 t0, vec4 t1, vec4 t2) {
    // PICA combiner buffer. `buf` is what PREVBUF reads; `nextbuf` is the pending value.
    // Azahar assigns buf <- nextbuf AFTER the stage output is computed and only THEN applies the
    // latch (sw_rasterizer.cpp ~991 and glsl_fs_shader_gen.cpp ~476 agree), so a latched value
    // first becomes visible two stages later. The CMB stores the latch flag one stage AHEAD of
    // Azahar's 0-based mask bit — 3dbrew names the GPUREG_TEXENV_UPDATE_BUFFER bits "TEV stage
    // 1..4" and Grezzo puts the flag on the named stage — hence the stage s+1 lookup below.
    // Confirmed on data: all 14 PREVBUF-reading materials in the ROM latch at exactly read-1,
    // which is meaningful only under this mapping (childlink_v2 mat26, the Goron bracelet:
    // stage 0 diffuse -> buffer, stage 1 replaces PREVIOUS with the env term, stage 2 adds the
    // diffuse back from the buffer; without this the bracelet renders black).
    vec4 prev = vec4(0.0);
    vec4 buf = vec4(0.0);
    vec4 nextbuf = vec4(0.0);
    int n = int(ubo.uTevCtl.x + 0.5);
    for (int s = 0; s < n; s++) {
        uvec4 w = ubo.uTevStages[s];
        uint kidx = (w.y >> 24) & 7u;
        vec4 sa = tevSrc(w.x & 15u, prim, t0, t1, t2, prev, buf, kidx);
        vec4 sb = tevSrc((w.x >> 4) & 15u, prim, t0, t1, t2, prev, buf, kidx);
        vec4 sc = tevSrc((w.x >> 8) & 15u, prim, t0, t1, t2, prev, buf, kidx);
        vec3 ca = tevColorMod(w.y & 15u, sa);
        vec3 cb = tevColorMod((w.y >> 4) & 15u, sb);
        vec3 cc = tevColorMod((w.y >> 8) & 15u, sc);
        vec3 rgb = tevColorOp(w.z & 15u, ca, cb, cc);
        rgb = clamp(rgb * float(1u << ((w.z >> 8) & 3u)), 0.0, 1.0);
        float alpha;
        if (((w.z >> 4) & 15u) == 7u) {
            alpha = rgb.r;
        } else {
            vec4 aa = tevSrc((w.x >> 12) & 15u, prim, t0, t1, t2, prev, buf, kidx);
            vec4 ab = tevSrc((w.x >> 16) & 15u, prim, t0, t1, t2, prev, buf, kidx);
            vec4 ac = tevSrc((w.x >> 20) & 15u, prim, t0, t1, t2, prev, buf, kidx);
            float fa = tevAlphaMod((w.y >> 12) & 15u, aa);
            float fb = tevAlphaMod((w.y >> 16) & 15u, ab);
            float fc = tevAlphaMod((w.y >> 20) & 15u, ac);
            alpha = tevAlphaOp((w.z >> 4) & 15u, fa, fb, fc);
        }
        alpha = clamp(alpha * float(1u << ((w.z >> 10) & 3u)), 0.0, 1.0);
        prev = vec4(rgb, alpha);
        buf = nextbuf;
        uint latch = (s + 1 < n) ? ubo.uTevStages[s + 1].z : 0u;
        if ((latch & 4096u) != 0u) nextbuf.rgb = prev.rgb;
        if ((latch & 8192u) != 0u) nextbuf.a = prev.a;
    }
    return prev;
}
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
        if (ubo.uSheen.y > 3.5) {
    // Mode 4 (mat10/11 self-sphere-add): coordinator-0 is also sphere-mapped, so re-sample
    // tex0 at the sphere UV. Result: 2*TEX0 + TEX1 = 3*TEX0 (tex1=tex0, same sphere UV).
            vec3 t0s = texture(uTex, vUv1).rgb;
            t.rgb = clamp(t0s * ubo.uSheen.z + t1, 0.0, 1.0);
        } else if (ubo.uSheen.y > 2.5) {
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
    if (!tevG && t.a < ubo.uParams.z) discard;
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
    // (zelda3d_gl.cpp's per-model light-dir override).
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
    } else if (ubo.uParams.y > 0.5) {
        prim = vec4(vColor.rgb * shade, vColor.a);
    } else {
        prim = vec4(vColor.rgb, vColor.a);
    }
    // Generic per-stage TEV path (render.multi-stage-tev): evaluate the material's real
    // combiner chain — multi-texture, multi-stage, per-stage ops/operands/scales/consts —
    // instead of the single-MODULATE legacy compound below. This is what "Zora's water is dark
    // and desaturated" was: tex1/tex2 + MultiplyThenAdd stages our fixed pipeline dropped.
    if (tevG) {
        vec4 t1s = texture(uTex1, vUv1);
        vec4 t2s = texture(uTex2, vUv2);
        vec4 tev = tevRun(prim, t, t1s, t2s);
        if (tev.a < ubo.uParams.z) discard;
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


// SgUbo, kSgCommonBytes, kSgBonesBytes and the <=4096-byte push-block invariants live in
// fast/zelda3d_sg_ubo.h (single source of truth; unit-tested in tests/zelda3d_render_tests.cpp).

// The renderer's record types (SgGroup / SgModel / GeomRec / PipeKey / DepthDraw) and ALL the former
// file-scope `g_*` module state now live as members of Fast::Zelda3DRenderer (fast/backends/
// zelda3d_sdl3gpu.h); the helper/API function bodies below are unchanged member-function definitions
// (member access is the implicit `this->`). g_provider stays a file-scope global because it is set
// (Zelda3D_Sg_SetProvider) independently of the backend/device lifecycle, so it must not be tied to the
// instance's lifetime.
Zelda3DModelProvider g_provider = nullptr;

// ---- helpers ----
SDL_GPUSamplerAddressMode wrapMode(unsigned glWrap) {
    switch (glWrap) {
        case 0x2900: // GL_CLAMP
        case 0x812F: // GL_CLAMP_TO_EDGE
            return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        case 0x8370: // GL_MIRRORED_REPEAT
            return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
        default: // 0x2901 GL_REPEAT
            return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    }
}

// True for the additive-blend point-sprite VFX class (stars, sparkles, glow) that must sample
// only the CMB's real, single native texture level — see getSampler's noMip comment.
bool isAdditiveBlendGroup(const SgGroup& g) {
    return g.blendEnable && g.bSrcRGB == 0x0302 /*SRC_ALPHA*/ && g.bDstRGB == 0x0001 /*ONE*/;
}

} // namespace

SDL_GPUSampler* Fast::Zelda3DRenderer::getSampler(unsigned wrapS, unsigned wrapT, bool noMip) {
    // Use the backend's single sampler cache. The model path needs LINEAR + max_lod=1000: left at the
    // zero default, a LINEAR-minified large texture (e.g. the 2048² Kokiri ground) computes an LOD > 0
    // that clamps against max_lod=0 and samples nothing -> the surface renders BLACK. Only large/
    // minified textures hit it (small ones stay at LOD 0), which is why the terrain vanished but
    // props/sky did not. max_lod=1000 lands in a distinct cache slot from the N64 samplers (max_lod=0).
    //
    // `noMip` (title star-brightness residual, debug_journal/2026-07-10): every CMB texture Zelda3D
    // uploads gets a SYNTHETIC full mip chain generated at upload time (uploadTexture below) — the CMB
    // format itself never carries baked mip levels (verified byte-for-byte: fine_star.cmb's texture
    // entry data_len=4096 for a 64x64 L8 texture = exactly ONE level's worth of bytes, no room for a
    // chain), so the PICA200 hardware/Citra always samples that single level, full detail, no LOD blur.
    // For opaque/modulate materials (terrain, walls) the synthetic chain is a deliberate, worthwhile
    // antialiasing enhancement over strict fidelity (grazing-angle shimmer, #134). But for a materially
    // different class — small ADDITIVE point-sprite-style VFX textures (stars, sparkles: blend
    // src=SRC_ALPHA(0x302) dst=ONE(0x1), matching fine_star's own material bytes) — mip-averaging is
    // actively wrong: it blends each sparse bright texel into its near-black neighbors, which crushes
    // the peak (measured SoH/Az peak ratio 0.73-0.80) while leaving the LOCAL average roughly intact
    // (measured integrated ratio ~1.03x) — exactly the "peak low, integrated matches" signature
    // documented in debug_journal/2026-07-08-title-star-brightness-L8-decode.md §4. Forcing max_lod=0
    // for these draws makes the sampler read only the real, single, unblurred level — matching the
    // oracle's own hardware behavior exactly, not a fitted brightness constant.
    float maxLod = noMip ? 0.0f : 1000.0f;
    return Fast::g_activeSdl3GpuApi->GetOrCreateSamplerEx(SDL_GPU_FILTER_LINEAR, wrapMode(wrapS), wrapMode(wrapT),
                                                          maxLod);
}

namespace {

// GL blend enum -> SDL3 GPU blend factor.
SDL_GPUBlendFactor mapFactor(unsigned f) {
    switch (f) {
        case 0: return SDL_GPU_BLENDFACTOR_ZERO;
        case 1: return SDL_GPU_BLENDFACTOR_ONE;
        case 0x300: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
        case 0x301: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
        case 0x302: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        case 0x303: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        case 0x304: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
        case 0x305: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
        case 0x306: return SDL_GPU_BLENDFACTOR_DST_COLOR;
        case 0x307: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
        case 0x308: return SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
        default: return SDL_GPU_BLENDFACTOR_ONE; // constant-color factors unsupported (rare); ONE
    }
}
SDL_GPUBlendOp mapEq(unsigned e) {
    switch (e) {
        case 0x8006: return SDL_GPU_BLENDOP_ADD;
        case 0x800A: return SDL_GPU_BLENDOP_SUBTRACT;
        case 0x800B: return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
        case 0x8007: return SDL_GPU_BLENDOP_MIN;
        case 0x8008: return SDL_GPU_BLENDOP_MAX;
        default: return SDL_GPU_BLENDOP_ADD;
    }
}

} // namespace

// Upload an RGBA8 texture (one-shot copy pass on a private command buffer).
SDL_GPUTexture* Fast::Zelda3DRenderer::uploadTexture(int w, int h, const unsigned char* rgba) {
    if (w <= 0 || h <= 0)
        w = h = 1;
    // Full mip chain: without it, a repeating/detailed texture viewed at a grazing angle (e.g. a
    // room wall) aliases badly under plain bilinear sampling — the sampler already samples up to
    // max_lod=1000 (see getSampler), but with only 1 level present that has nothing to select.
    // COLOR_TARGET usage is needed alongside SAMPLER because SDL_GenerateMipmapsForGPUTexture
    // downsamples via blit passes, which write each level as a render target.
    // FALSIFIED LEAD (2026-07-22, render.kokiri-near-terrain-overbright): the synthetic chain was
    // the standing suspect for Kokiri's near-terrain +18% (a distance-dependent error with a
    // distance-dependent mechanism). It is NOT. Built with the chain disabled (mipLevels=1 AND
    // sampler max_lod=0 — max_lod=1000 over a single-level texture renders BLACK on this backend),
    // vanilla-on-vanilla at the matched camera, draw d8 moved 1.184 -> 1.181 over its 126682
    // exclusive pixels. Do not re-run this experiment.
    int mipLevels = 1;
    for (int m = (w > h ? w : h); m > 1; m >>= 1)
        mipLevels++;
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ci.width = (uint32_t)w;
    ci.height = (uint32_t)h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = (uint32_t)mipLevels;
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(g_device, &ci);
    if (!tex)
        fprintf(stderr, "[Zelda3D_SG] CreateGPUTexture %dx%d FAILED: %s\n", w, h, SDL_GetError());

    const uint32_t size = (uint32_t)w * h * 4;
    static const unsigned char white[4] = { 255, 255, 255, 255 };
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = rgba ? size : 4;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(g_device, &tci);
    if (!tb)
        fprintf(stderr, "[Zelda3D_SG] CreateTransferBuffer %u bytes (%dx%d) FAILED: %s\n", tci.size, w, h,
                SDL_GetError());
    void* mapped = SDL_MapGPUTransferBuffer(g_device, tb, false);
    if (!mapped)
        fprintf(stderr, "[Zelda3D_SG] MapTransferBuffer %u bytes (%dx%d) FAILED: %s\n", tci.size, w, h,
                SDL_GetError());
    memcpy(mapped, rgba ? rgba : white, rgba ? size : 4);
    SDL_UnmapGPUTransferBuffer(g_device, tb);
    SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(g_device);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
    SDL_GPUTextureTransferInfo ti{};
    ti.transfer_buffer = tb;
    ti.pixels_per_row = (uint32_t)w;
    ti.rows_per_layer = (uint32_t)h;
    SDL_GPUTextureRegion reg{};
    reg.texture = tex;
    reg.w = (uint32_t)w;
    reg.h = (uint32_t)h;
    reg.d = 1;
    SDL_UploadToGPUTexture(cp, &ti, &reg, false);
    SDL_EndGPUCopyPass(cp);
    if (mipLevels > 1)
        SDL_GenerateMipmapsForGPUTexture(c, tex);
    SDL_SubmitGPUCommandBuffer(c);
    SDL_ReleaseGPUTransferBuffer(g_device, tb);
    return tex;
}

bool Fast::Zelda3DRenderer::ensureResources() {
    if (g_resReady)
        return true;
    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;
    if (!api)
        return false;
    g_device = api->GpuDevice();
    if (!g_device)
        return false;

    // ZELDA3D_SG_FRAGDBG=<1..5>: replace the combiner with an isolated stage so a black/missing draw is
    // diagnosed by VALUE — 1=texture only, 2=raw vertex colour, 3=solid white, 4=shade(flat tint),
    // 5=PRIMARY_COLOR (the vertex-lit o1 the TEV consumes; the direct counterpart of the oracle's
    // `PIXEL ... primary=` probe). Applied to EVERY Zelda3D draw unconditionally (unlike a per-draw
    // uniform gate), so the readback is trustworthy. Measure with the REPL `region` tool, not by eye.
    // The anchor is the line that OPENS the combiner section: it must sit after `prim` is computed and
    // before the TEV/CONSTANT/stage-scale/fog stages, so the probe is not repainted by fog. (It used to
    // anchor on `vec3 rgb = t.rgb * vColor.rgb * shade;`, a line the lighting-port rewrite deleted —
    // FRAGDBG had been silently INERT ever since; found 2026-07-22.)
    // FRAGDBG picks ONE of the template's named taps. The mode->snippet table below is unchanged;
    // what changed is how it lands. This used to be fragSrc.find() on a literal source LINE, and when
    // the lighting-port rewrite deleted the line it matched, the probe silently reported nothing and
    // read as a clean result for weeks. A named hole cannot drift out from under its filler, and the
    // fill is verified after rendering rather than assumed.
    const char* tapCombiner = "";
    const char* tapPreFog = "";
    int fragdbgMode = 0;
    if (const char* dbg = getenv("ZELDA3D_SG_FRAGDBG")) {
        fragdbgMode = atoi(dbg);
        const int mode = fragdbgMode;
        // ALL taps sit AFTER the combiner block, so the material's real ALPHA TEST (the tevG
        // path discards on the FINAL combiner alpha) still runs: an earlier anchor made
        // alpha-tested foliage paint solid over the ground and silently corrupted every
        // mask-restricted readback taken from a probe frame (found 2026-07-22, after a
        // "our texture is 15% dark" reading that the fog A/B falsified).
        // Each snippet returns IMMEDIATELY so the override bypasses the later combiner/ambient/FOG
        // stages — otherwise the fog mix (fog colour ~= the scene tan) repaints the probe.
        const char* inject = mode == 1 ? "frag = vec4(t.rgb, 1.0); return;\n"
                             : mode == 2 ? "frag = vec4(vColor.rgb, 1.0); return;\n"
                             : mode == 3 ? "frag = vec4(1.0); return;\n"
                             : mode == 4 ? "frag = vec4(shade, 1.0); return;\n"
                             : mode == 5 ? "frag = vec4(prim.rgb, 1.0); return;\n"
                             // Mode 8 is the COLOUR-SPACE RAMP: known constants (0.25, 0.5, 0.75) out of
                             // the shader, so a readback says what the path between here and the PNG does
                             // to a value. It exists because every ours-vs-oracle number is compared
                             // against Azahar's software rasterizer, whose PIXEL lines are raw linear
                             // 8-bit — and an assumed-but-unvalidated sRGB conversion on our side would
                             // silently reassign the divergence to the wrong channel. Linear passthrough
                             // reads (64,128,191); sRGB encoding on write reads about (137,188,225).
                             : mode == 8 ? "frag = vec4(0.25, 0.5, 0.75, 1.0); return;\n"
                                         : "";
        // Mode 6 taps the combiner result before the CONSTANT/stage-scale/FOG stages — the direct
        // counterpart of the oracle's `PIXEL ... combined=` field. Mode 7 taps after the CONSTANT +
        // stage-scale stages, immediately BEFORE fog.
        if (mode == 7) {
            tapPreFog = "    frag = vec4(rgb, 1.0); return;\n";
        } else if (mode == 6) {
            tapCombiner = "    frag = vec4(rgb, 1.0); return;\n";
        } else {
            tapCombiner = inject;
        }
    }

    inja::json data;
    data["ubo_body"] = SG_UBO_COMMON_BODY;        // the macros stay the single source of truth for
    data["ubo_bones_body"] = SG_UBO_BONES_BODY;   // the UBO layout shared with the C++ struct
    data["tap_combiner"] = tapCombiner;
    data["tap_pre_fog"] = tapPreFog;

    std::string vertSrc, fragSrc;
    try {
        data["q"] = "out";
        data["varyings"] = inja::render(kVaryings, data);
        vertSrc = inja::render(kVertTemplate, data);
        data["q"] = "in";
        data["varyings"] = inja::render(kVaryings, data);
        fragSrc = inja::render(kFragTemplate, data);
    } catch (const std::exception& e) {
        fprintf(stderr, "[Zelda3D_SG] shader template render FAILED: %s\n", e.what());
        return false;
    }

    // Verify the tap actually landed. The whole point of moving off find(anchor) was that a probe
    // which quietly injects nothing is indistinguishable from a probe that found no signal.
    if (fragdbgMode != 0) {
        const char* want = *tapPreFog ? tapPreFog : tapCombiner;
        if (*want && fragSrc.find(want) == std::string::npos) {
            fprintf(stderr, "[Zelda3D_SG] FRAGDBG mode=%d: TAP DID NOT LAND — probe inert\n", fragdbgMode);
        } else {
            fprintf(stderr, "[Zelda3D_SG] FRAGDBG mode=%d active\n", fragdbgMode);
        }
    }

    // ZELDA3D_DUMP_SHADERS=<dir> writes the FINAL GLSL actually handed to the compiler. The shader
    // is assembled from templates and then patched further by FRAGDBG, so "what the source file
    // says" and "what the GPU ran" are not the same text — this dumps the latter, which is the only
    // version worth diffing when a shader refactor has to prove it changed nothing.
    if (const char* dumpDir = getenv("ZELDA3D_DUMP_SHADERS")) {
        auto dump = [&](const char* name, const char* text) {
            const std::string path = std::string(dumpDir) + "/" + name;
            if (FILE* f = fopen(path.c_str(), "wb")) {
                fwrite(text, 1, strlen(text), f);
                fclose(f);
                fprintf(stderr, "[Zelda3D_SG] dumped shader -> %s\n", path.c_str());
            } else {
                fprintf(stderr, "[Zelda3D_SG] CANNOT write shader dump %s\n", path.c_str());
            }
        };
        dump("zelda3d_sg.vert", vertSrc.c_str());
        dump("zelda3d_sg.frag", fragSrc.c_str());
    }

    std::vector<uint32_t> vsSpv, fsSpv;
    if (!CompileGlsl(EShLangVertex, vertSrc.c_str(), vsSpv) || !CompileGlsl(EShLangFragment, fragSrc.c_str(), fsSpv))
        return false;

    SDL_GPUShaderCreateInfo vci{};
    vci.code_size = vsSpv.size() * sizeof(uint32_t);
    vci.code = (const Uint8*)vsSpv.data();
    vci.entrypoint = "main";
    vci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vci.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    // kVert declares TWO vertex uniform buffers: set=1 binding=0 UBO (common) + set=1 binding=1
    // UBOBones (bone matrices for GPU skinning). This count is the pipeline-layout's vertex uniform
    // descriptor count and MUST match the shader, or binding 1 (bones) has no descriptor slot: the
    // Vulkan validation layer flags it (VUID-VkGraphicsPipelineCreateInfo-layout-07988 +
    // "Set 1, Binding 1, bones is invalid") and the bones descriptor is dereferenced unbacked at draw
    // time — a stale/garbage read that lavapipe-serial tolerates but MoltenVK and multi-threaded
    // lavapipe fault on (the headless SKYBUG crash / the macOS BindFragmentSamplers crash).
    vci.num_uniform_buffers = 2;
    g_vert = SDL_CreateGPUShader(g_device, &vci);

    SDL_GPUShaderCreateInfo fci{};
    fci.code_size = fsSpv.size() * sizeof(uint32_t);
    fci.code = (const Uint8*)fsSpv.data();
    fci.entrypoint = "main";
    fci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fci.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fci.num_samplers = 3;        // uTex + uShadowMap + uTex1 (dual-texture detail mask)
    fci.num_uniform_buffers = 1; // UBO
    g_frag = SDL_CreateGPUShader(g_device, &fci);

    if (!g_vert || !g_frag) {
        fprintf(stderr, "[Zelda3D_SG] shader create failed: %s\n", SDL_GetError());
        return false;
    }

    g_resReady = true;
    fprintf(stderr, "[Zelda3D_SG] resources ready (unified op model)\n");
    return true;
}

SDL_GPUGraphicsPipeline* Fast::Zelda3DRenderer::getPipeline(const SgGroup& g, int frontCW) {
    bool doCull = g.faceCull && sgFaceCullOn();
    PipeKey key;
    key.v = { (uint32_t)((g.blendEnable ? 1u : 0u) | (g.depthWrite ? 2u : 0u) | (doCull ? 4u : 0u) |
                         (doCull && frontCW ? 8u : 0u)),
              g.bSrcRGB, g.bDstRGB, g.bEqRGB, g.bSrcA, g.bDstA, g.bEqA, 0u };
    auto it = g_pipelines.find(key);
    if (it != g_pipelines.end())
        return it->second;

    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;

    // Vertex input: Zelda3DGlVtx (pos3, nrm3, uv2, boneId4, boneW4, color4).
    SDL_GPUVertexAttribute attrs[6]{};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (uint32_t)offsetof(Zelda3DGlVtx, pos) };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (uint32_t)offsetof(Zelda3DGlVtx, nrm) };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(Zelda3DGlVtx, uv) };
    attrs[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (uint32_t)offsetof(Zelda3DGlVtx, boneIds) };
    attrs[4] = { 4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (uint32_t)offsetof(Zelda3DGlVtx, weights) };
    attrs[5] = { 5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (uint32_t)offsetof(Zelda3DGlVtx, color) };
    SDL_GPUVertexBufferDescription vb{};
    vb.slot = 0;
    vb.pitch = sizeof(Zelda3DGlVtx);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = g_vert;
    pci.fragment_shader = g_frag;
    pci.vertex_input_state.vertex_buffer_descriptions = &vb;
    pci.vertex_input_state.num_vertex_buffers = 1;
    pci.vertex_input_state.vertex_attributes = attrs;
    pci.vertex_input_state.num_vertex_attributes = 6;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = doCull ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face =
        frontCW ? SDL_GPU_FRONTFACE_CLOCKWISE : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.rasterizer_state.enable_depth_clip = false; // depth clamp (device feature), matches Fast3D

    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = g.depthWrite != 0;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pci.depth_stencil_state.enable_stencil_test = false;

    SDL_GPUColorTargetDescription ct{};
    ct.format = api->GpuColorFormat();
    if (g.blendEnable) {
        ct.blend_state.enable_blend = true;
        ct.blend_state.src_color_blendfactor = mapFactor(g.bSrcRGB);
        ct.blend_state.dst_color_blendfactor = mapFactor(g.bDstRGB);
        ct.blend_state.color_blend_op = mapEq(g.bEqRGB);
        ct.blend_state.src_alpha_blendfactor = mapFactor(g.bSrcA);
        ct.blend_state.dst_alpha_blendfactor = mapFactor(g.bDstA);
        ct.blend_state.alpha_blend_op = mapEq(g.bEqA);
    } else {
        ct.blend_state.enable_blend = false;
    }
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = api->GpuDepthFormat();

    SDL_GPUGraphicsPipeline* pipe = SDL_CreateGPUGraphicsPipeline(g_device, &pci);
    if (!pipe)
        fprintf(stderr, "[Zelda3D_SG] pipeline create failed: %s\n", SDL_GetError());
    g_pipelines[key] = pipe;
    return pipe;
}

SgModel* Fast::Zelda3DRenderer::ensureUploaded(int modelId) {
    SgModel& m = g_models[modelId];
    if (m.uploaded)
        return &m;
    if (m.failed)
        return nullptr;
    const Zelda3DGlGroup* groups = nullptr;
    const Zelda3DGlTex* texs = nullptr;
    int groupCount = 0, texCount = 0;
    if (!g_provider || !g_provider(modelId, &groups, &groupCount, &texs, &texCount) || groupCount <= 0) {
        fprintf(stderr, "[Zelda3D_SG] model %d unavailable from provider\n", modelId);
        m.failed = true;
        return nullptr;
    }

    std::vector<Zelda3DGlVtx> all;
    for (int i = 0; i < groupCount; i++) {
        SgGroup g;
        g.first = (uint32_t)all.size();
        g.count = (uint32_t)groups[i].vertCount;
        g.texIndex = groups[i].texIndex;
        g.alphaTest = groups[i].alphaTest;
        g.alphaRef = groups[i].alphaRef;
        g.wrapS = groups[i].wrapS;
        g.wrapT = groups[i].wrapT;
        g.blendEnable = groups[i].blendEnable;
        g.bSrcRGB = groups[i].blendSrcRGB;
        g.bDstRGB = groups[i].blendDstRGB;
        g.bEqRGB = groups[i].blendEqRGB;
        g.bSrcA = groups[i].blendSrcA;
        g.bDstA = groups[i].blendDstA;
        g.bEqA = groups[i].blendEqA;
        g.depthWrite = groups[i].depthWrite;
        g.polygonOffset = groups[i].polygonOffset;
        g.cull = groups[i].cull;
        g.faceCull = groups[i].faceCull;
        g.meshId = groups[i].meshId;
        g.materialIndex = groups[i].materialIndex;
        g.vertexLighting = groups[i].vertexLighting;
        g.fogEnabled = groups[i].fogEnabled;
        g.combScaleRGB = groups[i].combScaleRGB;
        for (int k = 0; k < 3; k++) {
            g.matAmbient[k] = groups[i].matAmbient[k];
            g.matDiffuse[k] = groups[i].matDiffuse[k];
        }
        for (int s = 0; s < 6; s++)
            for (int k = 0; k < 4; k++)
                g.matConstant[s][k] = groups[i].matConstant[s][k];
        g.combConstIdx = groups[i].combConstIdx;
        g.combUsesConst = groups[i].combUsesConst;
        g.combConstScaleRGB = (groups[i].combConstScaleRGB > 0.0f) ? groups[i].combConstScaleRGB : 1.0f;
        g.dualTexMode = groups[i].dualTexMode;
        g.tevGeneric = groups[i].tevGeneric;
        if (g.dualTexMode || g.tevGeneric) {
            g.dualTexScale2 = groups[i].dualTexScale2;
            g.tex1Index = groups[i].tex1Index;
            g.wrap1S = groups[i].wrap1S;
            g.wrap1T = groups[i].wrap1T;
            g.uv1Scale[0] = groups[i].uv1Scale[0];
            g.uv1Scale[1] = groups[i].uv1Scale[1];
            g.uv1Trans[0] = groups[i].uv1Trans[0];
            g.uv1Trans[1] = groups[i].uv1Trans[1];
            g.coord1Mapping = groups[i].coord1Mapping;
        }
        // Generic per-stage TEV chain (render.multi-stage-tev).
        if (g.tevGeneric) {
            g.tevStageCount = groups[i].tevStageCount;
            for (int s = 0; s < 6; s++)
                for (int k = 0; k < 3; k++)
                    g.tevStagePack[s][k] = groups[i].tevStagePack[s][k];
            g.tex2Index = groups[i].tex2Index;
            g.wrap2S = groups[i].wrap2S;
            g.wrap2T = groups[i].wrap2T;
            g.uv2Scale[0] = groups[i].uv2Scale[0];
            g.uv2Scale[1] = groups[i].uv2Scale[1];
            g.uv2Trans[0] = groups[i].uv2Trans[0];
            g.uv2Trans[1] = groups[i].uv2Trans[1];
            g.coord2Mapping = groups[i].coord2Mapping;
        }
        if (groups[i].vertCount > 0) {
            for (int k = 0; k < 4; k++)
                g.dbgColor0[k] = groups[i].verts[0].color[k];
            uint32_t vc = groups[i].vertCount;
            for (int k = 0; k < 2; k++) {
                g.dbgUv0[k] = groups[i].verts[0].uv[k];
                g.dbgUv1[k] = groups[i].verts[vc / 2].uv[k];
                g.dbgUv2[k] = groups[i].verts[vc - 1].uv[k];
            }
        }
        all.insert(all.end(), groups[i].verts, groups[i].verts + groups[i].vertCount);
        m.groups.push_back(g);
    }

    // Model-local AABB over all vertices (geomscan; see Zelda3D_GeomScanDump).
    if (!all.empty()) {
        for (int k = 0; k < 3; k++) m.localMin[k] = m.localMax[k] = all[0].pos[k];
        for (const Zelda3DGlVtx& v : all) {
            for (int k = 0; k < 3; k++) {
                if (v.pos[k] < m.localMin[k]) m.localMin[k] = v.pos[k];
                if (v.pos[k] > m.localMax[k]) m.localMax[k] = v.pos[k];
            }
        }
        m.hasBounds = true;
    }

    // Device vertex buffer via a transfer-buffer copy.
    const uint32_t vbBytes = (uint32_t)(all.size() * sizeof(Zelda3DGlVtx));
    if (vbBytes > 0) {
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bci.size = vbBytes;
        m.vbo = SDL_CreateGPUBuffer(g_device, &bci);
        SDL_GPUTransferBufferCreateInfo tci{};
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tci.size = vbBytes;
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(g_device, &tci);
        void* mapped = SDL_MapGPUTransferBuffer(g_device, tb, false);
        memcpy(mapped, all.data(), vbBytes);
        SDL_UnmapGPUTransferBuffer(g_device, tb);
        SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(g_device);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = tb;
        SDL_GPUBufferRegion dst{};
        dst.buffer = m.vbo;
        dst.size = vbBytes;
        SDL_UploadToGPUBuffer(cp, &src, &dst, false);
        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(c);
        SDL_ReleaseGPUTransferBuffer(g_device, tb);
    }

    for (int i = 0; i < texCount; i++) {
        m.textures.push_back(uploadTexture(texs[i].w, texs[i].h, texs[i].rgba));
        if (modelId == g_sgDumpModel || g_sgDumpTexActual == modelId || g_sgDumpTexAll) {
            if ((g_sgDumpTexActual == modelId || g_sgDumpTexAll) && texs[i].rgba) {
                // One-off raw-pixel dump (PPM, no library needed) so the SOURCE texel data can be
                // eyeballed directly, bypassing the whole render/sampler pipeline.
                char path[256];
                snprintf(path, sizeof(path), "scratch/sgtex_%d_%d.ppm", modelId, i);
                FILE* pf = fopen(path, "wb");
                if (pf) {
                    fprintf(pf, "P6\n%d %d\n255\n", texs[i].w, texs[i].h);
                    for (long p = 0; p < (long)texs[i].w * texs[i].h; p++)
                        fwrite(&texs[i].rgba[p * 4], 1, 3, pf);
                    fclose(pf);
                }
            }
            // Mean RGBA of the source texels (sgdump diagnostics: is the texture itself black?).
            const unsigned char* px = texs[i].rgba;
            long n = (long)texs[i].w * texs[i].h, sr = 0, sg = 0, sb = 0, sa = 0;
            if (px && n > 0)
                for (long p = 0; p < n; p++) {
                    sr += px[p * 4 + 0]; sg += px[p * 4 + 1]; sb += px[p * 4 + 2]; sa += px[p * 4 + 3];
                }
            // GPU readback of the just-uploaded texture: copy it to a download transfer buffer, wait,
            // map, and mean it. If this differs from srcMean, the UPLOAD is broken (not the sample).
            long gr = -1, gg = -1, gb = -1, ga = -1;
            SDL_GPUTexture* gtex = m.textures.back();
            if (gtex && n > 0) {
                SDL_GPUTransferBufferCreateInfo dci{};
                dci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
                dci.size = (uint32_t)(n * 4);
                SDL_GPUTransferBuffer* dl = SDL_CreateGPUTransferBuffer(g_device, &dci);
                SDL_GPUCommandBuffer* dc = SDL_AcquireGPUCommandBuffer(g_device);
                SDL_GPUCopyPass* dcp = SDL_BeginGPUCopyPass(dc);
                SDL_GPUTextureRegion dreg{};
                dreg.texture = gtex; dreg.w = texs[i].w; dreg.h = texs[i].h; dreg.d = 1;
                SDL_GPUTextureTransferInfo dti{};
                dti.transfer_buffer = dl; dti.pixels_per_row = texs[i].w; dti.rows_per_layer = texs[i].h;
                SDL_DownloadFromGPUTexture(dcp, &dreg, &dti);
                SDL_EndGPUCopyPass(dcp);
                SDL_GPUFence* f = SDL_SubmitGPUCommandBufferAndAcquireFence(dc);
                if (f) { SDL_WaitForGPUFences(g_device, true, &f, 1); SDL_ReleaseGPUFence(g_device, f); }
                const unsigned char* gp = (const unsigned char*)SDL_MapGPUTransferBuffer(g_device, dl, false);
                if (gp) {
                    long r2 = 0, g2 = 0, b2 = 0, a2 = 0;
                    for (long p = 0; p < n; p++) {
                        r2 += gp[p * 4 + 0]; g2 += gp[p * 4 + 1]; b2 += gp[p * 4 + 2]; a2 += gp[p * 4 + 3];
                    }
                    gr = r2 / n; gg = g2 / n; gb = b2 / n; ga = a2 / n;
                    SDL_UnmapGPUTransferBuffer(g_device, dl);
                }
                SDL_ReleaseGPUTransferBuffer(g_device, dl);
            }
            fprintf(stderr,
                    "[SG_DUMP] tex%-2d %dx%d srcMeanRGBA=(%ld,%ld,%ld,%ld) gpuMeanRGBA=(%ld,%ld,%ld,%ld) %s\n",
                    i, texs[i].w, texs[i].h, n ? sr / n : -1, n ? sg / n : -1, n ? sb / n : -1,
                    n ? sa / n : -1, gr, gg, gb, ga, px ? "" : "(null rgba!)");
        }
    }

    m.uploaded = true;
    fprintf(stderr, "[Zelda3D_SG] uploaded model %d: %d groups, %d textures, %zu verts\n", modelId, groupCount,
            texCount, all.size());
    return &m;
}

// ---------------------------------------------------------------------------
// Render-unification effort (kanban #131), Phase 2: CMB -> UnifiedVtx/UnifiedMaterial packers +
// the unified vertex buffer / pipeline builders. Only reached when gUnifiedRenderer & 1 (default
// off) — the old ensureUploaded/getPipeline path above is completely untouched.
// ---------------------------------------------------------------------------
namespace {

UnifiedVtx PackUnifiedVtx(const Zelda3DGlVtx& v, float combScaleRGB) {
    UnifiedVtx u{};
    // w=1.0: CMB is model-space, GPU-transformed via uMvp (alreadyTransformed=false) — see
    // unified_vtx.h's pos field doc.
    u.pos[0] = v.pos[0]; u.pos[1] = v.pos[1]; u.pos[2] = v.pos[2]; u.pos[3] = 1.0f;
    u.nrm[0] = v.nrm[0]; u.nrm[1] = v.nrm[1]; u.nrm[2] = v.nrm[2];
    u.uv0[0] = v.uv[0]; u.uv0[1] = v.uv[1];
    u.uv1[0] = 0.0f; u.uv1[1] = 0.0f;
    // No per-vertex clamp data on the CMB side (unlike N64) — a large no-op upper bound so the
    // unified fragment shader's clamp(uv, 0.5/texSize, texClamp) never actually clamps.
    u.texClamp[0] = 1e6f; u.texClamp[1] = 1e6f; u.texClamp[2] = 1e6f; u.texClamp[3] = 1e6f;
    // Stage-0 TEV RGB scale (comb_scale_rgb, cmb.h) folded in here since the combiner mux only has
    // room for a two-operand multiply (texel0 * vColor0) — Kokiri grass etc. MODULATE at x2/x4.
    for (int k = 0; k < 3; k++)
        u.color0[k] = (uint8_t)std::lround(std::clamp(v.color[k] * combScaleRGB, 0.0f, 1.0f) * 255.0f);
    u.color0[3] = (uint8_t)std::lround(std::clamp(v.color[3], 0.0f, 1.0f) * 255.0f);
    for (int k = 0; k < 4; k++)
        u.color1[k] = u.color2[k] = u.color3[k] = 0;
    u.fog[0] = 0.0f; u.fog[1] = 0.0f;
    for (int k = 0; k < 4; k++) {
        u.boneIds[k] = (uint8_t)std::clamp((int)std::lround(v.boneIds[k]), 0, 255);
        u.boneW[k] = (uint8_t)std::lround(std::clamp(v.weights[k], 0.0f, 1.0f) * 255.0f);
    }
    return u;
}

// CMB materials never exercise N64's 2-cycle/fog/grayscale combiner shapes — one texture, optional
// alpha test, is the whole structural space this content needs (see unified_shader.h's Variant).
Fast::Unified::Variant VariantForGroup(const SgGroup& g, bool hasTex) {
    if (!hasTex)
        return Fast::Unified::Variant::kUntextured;
    return g.alphaTest ? Fast::Unified::Variant::kSingleTexAlphaTest : Fast::Unified::Variant::kSingleTex;
}

} // namespace

Fast::SgModel* Fast::Zelda3DRenderer::ensureUnifiedUploaded(int modelId) {
    SgModel* base = ensureUploaded(modelId); // populates m.groups/textures if not already
    if (!base)
        return nullptr;
    SgModel& m = *base;
    if (m.unifiedUploaded)
        return &m;
    if (m.unifiedFailed)
        return nullptr;

    const Zelda3DGlGroup* groups = nullptr;
    const Zelda3DGlTex* texs = nullptr;
    int groupCount = 0, texCount = 0;
    if (!g_provider || !g_provider(modelId, &groups, &groupCount, &texs, &texCount) || groupCount <= 0) {
        m.unifiedFailed = true;
        return nullptr;
    }

    std::vector<UnifiedVtx> all;
    for (int i = 0; i < groupCount; i++) {
        // combScaleRGB is the CMB material's authored TEV stage-0 RGB scale (Kokiri grass ×2, etc)
        // — a static material property that runs unconditionally on the 3DS regardless of PICA
        // fragment-lighting state. Apply it whenever the material declares vertexLighting=1; do
        // NOT gate on gZelda3dWorldLit here, which only controls the ambient+diffuse*NdotL
        // computation (task #16: at title, that compute is off but combScaleRGB must remain).
        float scale = groups[i].vertexLighting ? groups[i].combScaleRGB : 1.0f;
        for (uint32_t k = 0; k < groups[i].vertCount; k++)
            all.push_back(PackUnifiedVtx(groups[i].verts[k], scale));
    }

    const uint32_t vbBytes = (uint32_t)(all.size() * sizeof(UnifiedVtx));
    if (vbBytes > 0) {
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bci.size = vbBytes;
        m.unifiedVbo = SDL_CreateGPUBuffer(g_device, &bci);
        SDL_GPUTransferBufferCreateInfo tci{};
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tci.size = vbBytes;
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(g_device, &tci);
        void* mapped = SDL_MapGPUTransferBuffer(g_device, tb, false);
        memcpy(mapped, all.data(), vbBytes);
        SDL_UnmapGPUTransferBuffer(g_device, tb);
        SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(g_device);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = tb;
        SDL_GPUBufferRegion dst{};
        dst.buffer = m.unifiedVbo;
        dst.size = vbBytes;
        SDL_UploadToGPUBuffer(cp, &src, &dst, false);
        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(c);
        SDL_ReleaseGPUTransferBuffer(g_device, tb);
    }
    m.unifiedUploaded = true;
    fprintf(stderr, "[Zelda3D_SG] unified-uploaded model %d: %zu verts\n", modelId, all.size());
    return &m;
}

SDL_GPUGraphicsPipeline* Fast::Zelda3DRenderer::getUnifiedPipeline(const SgGroup& g, int frontCW, int variant) {
    bool doCull = g.faceCull && sgFaceCullOn();
    PipeKey key;
    key.v = { (uint32_t)((g.blendEnable ? 1u : 0u) | (g.depthWrite ? 2u : 0u) | (doCull ? 4u : 0u) |
                         (doCull && frontCW ? 8u : 0u)),
              g.bSrcRGB, g.bDstRGB, g.bEqRGB, g.bSrcA, g.bDstA, g.bEqA, (uint32_t)variant };
    auto it = g_uniPipelines.find(key);
    if (it != g_uniPipelines.end())
        return it->second;

    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;

    if (g_uniVert[variant] == nullptr) {
        std::string vsrc = Fast::Unified::BuildVertexSource((Fast::Unified::Variant)variant);
        std::string fsrc = Fast::Unified::BuildFragmentSource((Fast::Unified::Variant)variant);
        // 1 UBO (UnifiedCommon) + 1 UBO (bones) for vertex; 1 sampler (untextured variant needs 0)
        // + 1 UBO (UnifiedCommon) for fragment — mirrors makeShader's existing (numSamplers, numUbo)
        // convention for the old fixed CMB shader.
        uint32_t numSamplers = (variant == (int)Fast::Unified::Variant::kUntextured) ? 0 : 1;
        g_uniVert[variant] = makeShader(vsrc.c_str(), EShLangVertex, 0, 2);
        g_uniFrag[variant] = makeShader(fsrc.c_str(), EShLangFragment, numSamplers, 1);
        if (!g_uniVert[variant] || !g_uniFrag[variant])
            fprintf(stderr, "[Zelda3D_SG] unified shader variant %d compile FAILED\n", variant);
    }
    if (!g_uniVert[variant] || !g_uniFrag[variant])
        return nullptr;

    // Vertex input: UnifiedVtx (pos4, nrm3, uv0_2, uv1_2, texClamp4, color0..3 x ubyte4norm, fog2,
    // boneIds ubyte4, boneW ubyte4norm) — see unified_vtx.h.
    SDL_GPUVertexAttribute attrs[12]{};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (uint32_t)offsetof(UnifiedVtx, pos) };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (uint32_t)offsetof(UnifiedVtx, nrm) };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(UnifiedVtx, uv0) };
    attrs[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(UnifiedVtx, uv1) };
    attrs[4] = { 4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (uint32_t)offsetof(UnifiedVtx, texClamp) };
    attrs[5] = { 5, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, (uint32_t)offsetof(UnifiedVtx, color0) };
    attrs[6] = { 6, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, (uint32_t)offsetof(UnifiedVtx, color1) };
    attrs[7] = { 7, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, (uint32_t)offsetof(UnifiedVtx, color2) };
    attrs[8] = { 8, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, (uint32_t)offsetof(UnifiedVtx, color3) };
    attrs[9] = { 9, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(UnifiedVtx, fog) };
    attrs[10] = { 10, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, (uint32_t)offsetof(UnifiedVtx, boneIds) };
    attrs[11] = { 11, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, (uint32_t)offsetof(UnifiedVtx, boneW) };
    SDL_GPUVertexBufferDescription vb{};
    vb.slot = 0;
    vb.pitch = sizeof(UnifiedVtx);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = g_uniVert[variant];
    pci.fragment_shader = g_uniFrag[variant];
    pci.vertex_input_state.vertex_buffer_descriptions = &vb;
    pci.vertex_input_state.num_vertex_buffers = 1;
    pci.vertex_input_state.vertex_attributes = attrs;
    pci.vertex_input_state.num_vertex_attributes = 12;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = doCull ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face =
        frontCW ? SDL_GPU_FRONTFACE_CLOCKWISE : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.rasterizer_state.enable_depth_clip = false;

    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = g.depthWrite != 0;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pci.depth_stencil_state.enable_stencil_test = false;

    SDL_GPUColorTargetDescription ct{};
    ct.format = api->GpuColorFormat();
    if (g.blendEnable) {
        ct.blend_state.enable_blend = true;
        ct.blend_state.src_color_blendfactor = mapFactor(g.bSrcRGB);
        ct.blend_state.dst_color_blendfactor = mapFactor(g.bDstRGB);
        ct.blend_state.color_blend_op = mapEq(g.bEqRGB);
        ct.blend_state.src_alpha_blendfactor = mapFactor(g.bSrcA);
        ct.blend_state.dst_alpha_blendfactor = mapFactor(g.bDstA);
        ct.blend_state.alpha_blend_op = mapEq(g.bEqA);
    } else {
        ct.blend_state.enable_blend = false;
    }
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = api->GpuDepthFormat();

    SDL_GPUGraphicsPipeline* pipe = SDL_CreateGPUGraphicsPipeline(g_device, &pci);
    if (!pipe)
        fprintf(stderr, "[Zelda3D_SG] unified pipeline create failed: %s\n", SDL_GetError());
    g_uniPipelines[key] = pipe;
    return pipe;
}

// Deferred model eviction (mirror of the GL/VK path). g_evictLo/g_evictHi/g_evictPending are members.
void Fast::Zelda3DRenderer::applyPendingEvict() {
    if (!g_evictPending || !g_device)
        return;
    g_evictPending = false;
    SDL_WaitForGPUIdle(g_device);
    for (auto it = g_models.begin(); it != g_models.end();) {
        if (it->first >= g_evictLo && it->first < g_evictHi) {
            SgModel& m = it->second;
            for (auto* t : m.textures)
                if (t)
                    SDL_ReleaseGPUTexture(g_device, t);
            if (m.vbo)
                SDL_ReleaseGPUBuffer(g_device, m.vbo);
            it = g_models.erase(it);
        } else {
            ++it;
        }
    }
}

namespace {

// One captured per-group draw, replayed inside the unified render pass.
struct DrawGroup {
    SDL_GPUGraphicsPipeline* pipeline;
    SDL_GPUTexture* tex;
    SDL_GPUSampler* samp;
    // Second texture binding (dual-texture detail mask, fire-glow). nullptr = none (the append
    // path substitutes the dummy texture so fragment sampler slot 2 is always backed).
    SDL_GPUTexture* tex1 = nullptr;
    SDL_GPUSampler* samp1 = nullptr;
    // Third texture binding (generic TEV, render.multi-stage-tev) — rides fragment sampler
    // slot 1 (the ex-shadow-map slot). nullptr = dummy-backed.
    SDL_GPUTexture* tex2 = nullptr;
    SDL_GPUSampler* samp2 = nullptr;
    uint32_t first, count;
    std::array<uint8_t, sizeof(SgUbo)> ubo;
};

} // namespace

// ====================================================================================================

// ====================================================================================================
// Public C-ABI (Zelda3D_Sg_* / Zelda3D_GeomScanDump). These keep their exact names + signatures (called
// from zelda3d_gl.cpp + zelda3d.c) and are now THIN SHIMS forwarding to the Zelda3DRenderer member
// subsystem on the live backend. When no SDL3 GPU backend is active the renderer is null and the
// shim no-ops (int-returning shims return 0), matching the former null-guard early returns. The
// function BODIES are the Zelda3DRenderer methods defined further below (bodies unchanged).
// ----------------------------------------------------------------------------------------------------
namespace {
inline Fast::Zelda3DRenderer* sgRenderer() {
    return Fast::g_activeSdl3GpuApi ? Fast::g_activeSdl3GpuApi->Soh3d() : nullptr;
}
} // namespace

extern "C" int Zelda3D_Sg_Active(void) {
    return Fast::g_activeSdl3GpuApi != nullptr ? 1 : 0;
}

extern "C" void Zelda3D_Sg_SetProvider(Zelda3DModelProvider fn) {
    g_provider = fn;
}

extern "C" int Zelda3D_GeomScanDump(int* modelIds, float* mins, float* maxs, int maxN) {
    if (auto* r = sgRenderer())
        return r->GeomScanDump(modelIds, mins, maxs, maxN);
    return 0;
}
extern "C" void Zelda3D_Sg_RequestEvictRange(int lo, int hi) {
    if (auto* r = sgRenderer())
        r->RequestEvictRange(lo, hi);
}
extern "C" void Zelda3D_Sg_BeginPass(void) {
    if (auto* r = sgRenderer())
        r->BeginPass();
}
extern "C" void Zelda3D_Sg_DrawModel(int modelId, const float* mp16, const float* mv16, int lit, int invertY,
                                   unsigned char r8, unsigned char g8, unsigned char b8, unsigned char a8,
                                   float aspectAdj, const float* boneData, int boneCnt, unsigned long long midMask,
                                   int sky, float uvOffU, float uvOffV, const void* matTex,
                                   const void* matConst, int forceUnlit, const float* lightDirOv,
                                   const float* sphRotOv) {
    // Zelda3D #140 render-side probe: log every DrawModel for the sun-billboard model id (2002 in
    // typical runs). Non-sky submits are the Navi emit (sun/moon are sky=1). Serves as the runtime
    // observable for tools/navi_close_test.py and to isolate whether an emit reaches the renderer,
    // is filtered inside DrawModel, or is drawn but produces no visible pixel.
    if (modelId == 2002) {
        static int sN = 0;
        if (++sN <= 20) {
            // mp16[12..14] is the model-matrix translation column (mat4 column-major), so it reports
            // WHERE the draw's origin sits — sun/moon are ~1000s of units away, Navi is at ~world pos.
            float tx = mp16 ? mp16[12] : 0.0f;
            float ty = mp16 ? mp16[13] : 0.0f;
            float tz = mp16 ? mp16[14] : 0.0f;
            fprintf(stderr, "[Zelda3D sgDraw #%d] modelId=%d sky=%d lit=%d rgba=(%d,%d,%d,%d) boneCnt=%d "
                   "mp_t=(%.1f,%.1f,%.1f)\n",
                   sN, modelId, sky, lit, r8, g8, b8, a8, boneCnt, tx, ty, tz);
            fflush(stdout);
        }
    }
    if (auto* r = sgRenderer())
        r->DrawModel(modelId, mp16, mv16, lit, invertY, r8, g8, b8, a8, aspectAdj, boneData, boneCnt, midMask, sky,
                     uvOffU, uvOffV, matTex, matConst, forceUnlit, lightDirOv, sphRotOv);
}
extern "C" void Zelda3D_Sg_EndPass(void) {
    if (auto* r = sgRenderer())
        r->EndPass();
}
extern "C" void Zelda3D_Sg_ClearOverlayDepth(void) {
    if (auto* r = sgRenderer())
        r->ClearOverlayDepth();
}

// geomscan bridge: copy the last completed frame's per-draw world AABBs out to the REPL (zelda3d.c
// `geomscan`, #115/#120). Returns the count written; modelIds[i], mins[i*3..], maxs[i*3..] = draw i.
// (Ported from the removed Vulkan backend; this is the SDL3 GPU definition of Zelda3D_GeomScanDump.)
int Fast::Zelda3DRenderer::GeomScanDump(int* modelIds, float* mins, float* maxs, int maxN) {
    int n = (int)g_geomLast.size();
    if (n > maxN) n = maxN;
    for (int i = 0; i < n; i++) {
        modelIds[i] = g_geomLast[i].modelId;
        for (int k = 0; k < 3; k++) {
            mins[i * 3 + k] = g_geomLast[i].wmin[k];
            maxs[i * 3 + k] = g_geomLast[i].wmax[k];
        }
    }
    return n;
}

void Fast::Zelda3DRenderer::RequestEvictRange(int lo, int hi) {
    g_evictLo = lo;
    g_evictHi = hi;
    g_evictPending = true;
}

SDL_GPUShader* Fast::Zelda3DRenderer::makeShader(const char* glsl, EShLanguage stage, uint32_t numSamplers,
                                               uint32_t numUbo) {
    std::vector<uint32_t> spv;
    if (!CompileGlsl(stage, glsl, spv))
        return nullptr;
    SDL_GPUShaderCreateInfo ci{};
    ci.code_size = spv.size() * sizeof(uint32_t);
    ci.code = (const Uint8*)spv.data();
    ci.entrypoint = "main";
    ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    ci.stage = (stage == EShLangVertex) ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    ci.num_samplers = numSamplers;
    ci.num_uniform_buffers = numUbo;
    return SDL_CreateGPUShader(g_device, &ci);
}

// #146 item B: fullscreen depth-only reset for the overlay depth scope (Zelda3D_Overlay2D_Begin).
// Fragment unconditionally writes gl_FragDepth=1.0 (far, this backend's 0=near/1=far convention).
// No color output (the pipeline disables the color write mask entirely, so the already-composited
// 3D scene's color is untouched); depth write ON, compare ALWAYS so this reset always lands.
static const char* kOverlayDepthFrag =
    "#version 450\n"
    "void main() {\n"
    "    gl_FragDepth = 1.0;\n"
    "}\n";

// #146 item B: one-time resources for the overlay depth-scope reset.
bool Fast::Zelda3DRenderer::ensureOverlayDepthResources() {
    if (g_overlayDepthResReady)
        return true;
    if (!ensureResources())
        return false;

    g_overlayDepthFrag = makeShader(kOverlayDepthFrag, EShLangFragment, /*samplers=*/0, /*ubo=*/0);
    if (!g_overlayDepthFrag) {
        fprintf(stderr, "[Zelda3D_SG] overlay depth-reset shader create failed: %s\n", SDL_GetError());
        return false;
    }
    // Reuses the model vertex shader slot? No — needs its own fullscreen-triangle vertex shader
    // (no vertex buffer, matches kAoCompVert's gl_VertexIndex trick) since g_vert expects the
    // full per-vertex model attribute layout. Compile a private copy so this reset has no
    // dependency on the AO module's lazy init/enable gating.
    static const char* kFsVert =
        "#version 450\n"
        "void main() {\n"
        "    vec2 p = vec2((gl_VertexIndex == 2) ? 3.0 : -1.0, (gl_VertexIndex == 1) ? 3.0 : -1.0);\n"
        "    gl_Position = vec4(p, 0.0, 1.0);\n"
        "}\n";
    SDL_GPUShader* vert = makeShader(kFsVert, EShLangVertex, 0, 0);
    if (!vert) {
        fprintf(stderr, "[Zelda3D_SG] overlay depth-reset vertex shader create failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = g_overlayDepthFrag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = true;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    pci.depth_stencil_state.enable_stencil_test = false;

    SDL_GPUColorTargetDescription ct{};
    ct.format = g_activeSdl3GpuApi->GpuColorFormat();
    ct.blend_state.enable_blend = false;
    ct.blend_state.enable_color_write_mask = true;
    ct.blend_state.color_write_mask = 0; // no color channels written — color buffer untouched
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = g_activeSdl3GpuApi->GpuDepthFormat();

    g_overlayDepthPipe = SDL_CreateGPUGraphicsPipeline(g_device, &pci);
    if (!g_overlayDepthPipe) {
        fprintf(stderr, "[Zelda3D_SG] overlay depth-reset pipeline create failed: %s\n", SDL_GetError());
        return false;
    }
    g_overlayDepthResReady = true;
    return true;
}

void Fast::Zelda3DRenderer::ClearOverlayDepth() {
    if (!g_ctxValid || !g_activeSdl3GpuApi)
        return;
    if (!ensureOverlayDepthResources() || !g_overlayDepthPipe)
        return;
    SDL_GPUViewport vp{};
    SDL_Rect sc{};
    g_activeSdl3GpuApi->GetZelda3DViewportScissor(vp, sc);
    // No UBO / texture — the shader is a constant fullscreen write.
    float dummyUbo[4] = { 0, 0, 0, 0 };
    g_activeSdl3GpuApi->AppendZelda3DFullscreen(g_overlayDepthPipe, dummyUbo, sizeof(dummyUbo),
                                              g_activeSdl3GpuApi->DummyTexture(), g_activeSdl3GpuApi->DummySampler(),
                                              vp, sc);
}

void Fast::Zelda3DRenderer::BeginPass() {
    g_ctxValid = false;
    g_sgDrawIdx = 0; // draw-isolation index is per-frame; reset here too in case EndPass was skipped
    // Seed the draw-isolation probe from the environment once, so it can be armed at launch (before
    // the REPL exists) as well as live. The REPL owns the globals afterwards.
    static bool sgProbeSeeded = false;
    if (!sgProbeSeeded) {
        sgProbeSeeded = true;
        if (const char* v = getenv("ZELDA3D_SG_DRAWONLY"))
            gZelda3dSgDrawOnly = atoi(v);
        if (const char* v = getenv("ZELDA3D_SG_DRAWLIST"))
            gZelda3dSgDrawList = (v[0] != '0');
    }
    // Publish the previous frame's geometry capture; start a fresh one for this frame's draws.
    g_geomLast.swap(g_geomCur);
    g_geomCur.clear();
    if (!g_activeSdl3GpuApi)
        return;
    if (!ensureResources())
        return;
    applyPendingEvict();
    g_ctxValid = true;
}

void Fast::Zelda3DRenderer::DrawModel(int modelId, const float* mp16, const float* mv16, int lit, int invertY,
                                    unsigned char r8, unsigned char g8, unsigned char b8, unsigned char a8,
                                    float aspectAdj, const float* boneData, int boneCnt, unsigned long long midMask,
                                    int sky, float uvOffU, float uvOffV, const void* matTex,
                                    const void* matConst, int forceUnlit, const float* lightDirOv,
                                    const float* sphRotOv) {
    const std::unordered_map<int, int>* matTexMap = static_cast<const std::unordered_map<int, int>*>(matTex);
    const std::unordered_map<int, Zelda3DMatConstOv>* matConstMap =
        static_cast<const std::unordered_map<int, Zelda3DMatConstOv>*>(matConst);
    if (!g_ctxValid)
        return;
    SgModel* m = ensureUploaded(modelId);
    if (!m || !m->vbo)
        return;

    // Geometry-value capture (geomscan): world AABB = local AABB transformed by mv16 (model->world,
    // column-major to match the shader's ubo.uMV * pos). One record per visible draw; the #115/#120
    // sweep reads these to flag misrendered geometry by VALUE (huge/degenerate extent), no diff.
    if (m->hasBounds && mv16 != nullptr && g_geomCur.size() < 4096) {
        GeomRec rec;
        rec.modelId = modelId;
        bool first = true;
        for (int c = 0; c < 8; c++) {
            float lx = (c & 1) ? m->localMax[0] : m->localMin[0];
            float ly = (c & 2) ? m->localMax[1] : m->localMin[1];
            float lz = (c & 4) ? m->localMax[2] : m->localMin[2];
            float wx = mv16[0] * lx + mv16[4] * ly + mv16[8] * lz + mv16[12];
            float wy = mv16[1] * lx + mv16[5] * ly + mv16[9] * lz + mv16[13];
            float wz = mv16[2] * lx + mv16[6] * ly + mv16[10] * lz + mv16[14];
            if (first) {
                rec.wmin[0] = rec.wmax[0] = wx; rec.wmin[1] = rec.wmax[1] = wy;
                rec.wmin[2] = rec.wmax[2] = wz; first = false;
            } else {
                if (wx < rec.wmin[0]) rec.wmin[0] = wx; if (wx > rec.wmax[0]) rec.wmax[0] = wx;
                if (wy < rec.wmin[1]) rec.wmin[1] = wy; if (wy > rec.wmax[1]) rec.wmax[1] = wy;
                if (wz < rec.wmin[2]) rec.wmin[2] = wz; if (wz > rec.wmax[2]) rec.wmax[2] = wz;
            }
        }
        g_geomCur.push_back(rec);
    }

    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;

    // RenderDoc-style per-draw inspection (REPL `sgdump <modelId>`): one-shot dump of every material
    // group's render state for one model, so a missing/invisible group is diagnosed by VALUE (which
    // state — alpha test, blend, cull, texture binding — kills it) instead of eyeballing the frame.
    if (modelId == g_sgDumpModel) {
        g_sgDumpModel = -1; // one-shot
        fprintf(stderr,
                "[SG_DUMP] model=%d groups=%zu lit=%d invertY=%d tint=(%u,%u,%u) a=%u aspectAdj=%.4f "
                "sky=%d worldLit=%d worldAmb=%.3f ambColor=(%.2f,%.2f,%.2f) fogOn=%d\n",
                modelId, m->groups.size(), lit, invertY, r8, g8, b8, a8, aspectAdj, sky, gZelda3dWorldLit,
                gZelda3dWorldAmb, gZelda3dWorldAmbColor[0], gZelda3dWorldAmbColor[1], gZelda3dWorldAmbColor[2],
                gZelda3dFogEnable);
        fprintf(stderr,
                "[SG_DUMP] model=%d fog3dOn=%d a=%.6f b=%.4f fogNear=%.1f fogFar=%.1f fogColor=(%.3f,%.3f,%.3f)\n",
                modelId, gZelda3dFog3dOn, gZelda3dFog3d[0], gZelda3dFog3d[1], gZelda3dFog3d[2], gZelda3dFog3d[3],
                gZelda3dFogColor[0], gZelda3dFogColor[1], gZelda3dFogColor[2]);
        int gi = -1;
        for (const SgGroup& grp : m->groups) {
            gi++;
            const bool hasTex =
                grp.texIndex >= 0 && grp.texIndex < (int)m->textures.size() && m->textures[grp.texIndex];
            fprintf(stderr,
                    "[SG_DUMP]  g%-2d cull=%d faceCull=%d meshId=%d tex=%d%s mat=%d vtxLit=%d combScale=%.3f "
                    "blend=%d(src=%#06x dst=%#06x) aTest=%d aRef=%.3f depthW=%d polyOff=%.4f first=%u count=%u "
                    "vColor0=(%.3f,%.3f,%.3f,%.3f) matAmb=(%.2f,%.2f,%.2f) matDif=(%.2f,%.2f,%.2f) "
                    "uv=[(%.2f,%.2f)(%.2f,%.2f)(%.2f,%.2f)] wrap=(%#06x,%#06x) "
                    "tex1=%d dualMode=%d dualScale2=%.1f constScale=%.1f uv1Xf=(%.2f,%.2f,%.2f,%.4f)\n",
                    gi, grp.cull, grp.faceCull, grp.meshId, grp.texIndex, hasTex ? "" : "(MISSING->dummy)",
                    grp.materialIndex, grp.vertexLighting, grp.combScaleRGB, grp.blendEnable, grp.bSrcRGB,
                    grp.bDstRGB, grp.alphaTest, grp.alphaRef, grp.depthWrite, grp.polygonOffset, grp.first,
                    grp.count, grp.dbgColor0[0], grp.dbgColor0[1], grp.dbgColor0[2], grp.dbgColor0[3],
                    grp.matAmbient[0], grp.matAmbient[1], grp.matAmbient[2], grp.matDiffuse[0],
                    grp.matDiffuse[1], grp.matDiffuse[2], grp.dbgUv0[0], grp.dbgUv0[1], grp.dbgUv1[0],
                    grp.dbgUv1[1], grp.dbgUv2[0], grp.dbgUv2[1], grp.wrapS, grp.wrapT, grp.tex1Index,
                    grp.dualTexMode, grp.dualTexScale2, grp.combConstScaleRGB, grp.uv1Scale[0], grp.uv1Scale[1],
                    grp.uv1Trans[0], grp.uv1Trans[1]);
            // PICA200 TEV constant palette + stage-0 selector — dumped on its own line so the
            // main SG_DUMP row stays parseable by existing tools; format:
            //   [SG_DUMP]   g<n> constIdx=<i> const0..const5=(r,g,b,a) x 6
            fprintf(stderr,
                    "[SG_DUMP]  g%-2d combUsesConst=%d constIdx=%d "
                    "const0=(%.3f,%.3f,%.3f,%.3f) const1=(%.3f,%.3f,%.3f,%.3f) "
                    "const2=(%.3f,%.3f,%.3f,%.3f) const3=(%.3f,%.3f,%.3f,%.3f) "
                    "const4=(%.3f,%.3f,%.3f,%.3f) const5=(%.3f,%.3f,%.3f,%.3f)\n",
                    gi, grp.combUsesConst, grp.combConstIdx,
                    grp.matConstant[0][0], grp.matConstant[0][1], grp.matConstant[0][2], grp.matConstant[0][3],
                    grp.matConstant[1][0], grp.matConstant[1][1], grp.matConstant[1][2], grp.matConstant[1][3],
                    grp.matConstant[2][0], grp.matConstant[2][1], grp.matConstant[2][2], grp.matConstant[2][3],
                    grp.matConstant[3][0], grp.matConstant[3][1], grp.matConstant[3][2], grp.matConstant[3][3],
                    grp.matConstant[4][0], grp.matConstant[4][1], grp.matConstant[4][2], grp.matConstant[4][3],
                    grp.matConstant[5][0], grp.matConstant[5][1], grp.matConstant[5][2], grp.matConstant[5][3]);
            // Generic TEV chain (render.multi-stage-tev): the packed per-stage words the shader
            // will decode (packing documented at Zelda3DGlGroup::tevStagePack).
            if (grp.tevGeneric) {
                fprintf(stderr,
                        "[SG_DUMP]  g%-2d tevGeneric=1 stages=%d tex2=%d coordMap=(%d,%d) "
                        "uv2Xf=(%.2f,%.2f,%.2f,%.4f) pack=",
                        gi, grp.tevStageCount, grp.tex2Index, grp.coord1Mapping, grp.coord2Mapping,
                        grp.uv2Scale[0], grp.uv2Scale[1], grp.uv2Trans[0], grp.uv2Trans[1]);
                for (int s = 0; s < grp.tevStageCount && s < 6; s++)
                    fprintf(stderr, "%s%08x/%08x/%08x", s ? "," : "", grp.tevStagePack[s][0],
                            grp.tevStagePack[s][1], grp.tevStagePack[s][2]);
                fprintf(stderr, "\n");
            }
        }
    }

    // Base UBO shared by all groups (per-group alphaRef/depthOffset/etc. patched below).
    SgUbo base{};
    memcpy(base.uMP, mp16, sizeof(base.uMP));
    base.uMP[0] *= aspectAdj; // mirror Fast3D AdjXForAspectRatio (MP column 0)
    base.uMP[4] *= aspectAdj;
    base.uMP[8] *= aspectAdj;
    base.uMP[12] *= aspectAdj;
    memcpy(base.uMV, mv16, sizeof(base.uMV));
    for (int k = 0; k < ZELDA3D_GL_MAX_BONES; k++)
        for (int e = 0; e < 16; e++)
            base.uBones[k * 16 + e] = (e % 5 == 0) ? 1.0f : 0.0f;
    if (boneData && boneCnt > 0) {
        // boneData is row-major (M*v). std140 mat4 is column-major with no transpose-on-upload, so
        // transpose CPU-side to match the GL path (which uploads with GL_TRUE transpose).
        int nb = boneCnt < ZELDA3D_GL_MAX_BONES ? boneCnt : ZELDA3D_GL_MAX_BONES;
        for (int k = 0; k < nb; k++) {
            const float* s = boneData + k * 16;
            float* d = base.uBones + k * 16;
            for (int rr = 0; rr < 4; rr++)
                for (int col = 0; col < 4; col++)
                    d[col * 4 + rr] = s[rr * 4 + col];
        }
    }
    base.uParams[0] = invertY ? -1.0f : 1.0f;
    base.uParams[1] = lit ? 1.0f : 0.0f;
    base.uTintSkin[0] = r8 / 255.0f;
    base.uTintSkin[1] = g8 / 255.0f;
    base.uTintSkin[2] = b8 / 255.0f;
    base.uTintSkin[3] = (boneData && boneCnt > 0) ? 1.0f : 0.0f;
    if (sphRotOv) {
        // Sphere-map view-rotation override (see zelda3d_sg_ubo.h uSphRot* comment): sphRotOv is
        // the row-major 3x3 view-rotation the caller derived from the live camera. uSphRot0.w is
        // the shader-side gate. base{} zero-init keeps it off for every draw that doesn't set it.
        memcpy(base.uSphRot0, sphRotOv + 0, 3 * sizeof(float));
        memcpy(base.uSphRot1, sphRotOv + 3, 3 * sizeof(float));
        memcpy(base.uSphRot2, sphRotOv + 6, 3 * sizeof(float));
        base.uSphRot0[3] = 1.0f;
    }
    if (lightDirOv) {
        // Wordmark sheen (title_logo_actor.md §6.3): lightDirOv is OBJECT-space (same space as
        // this model's own vertex normals aNrm) — transform by THIS draw's own mv16 (column-major,
        // matching the vertex shader's `vNrmView = mat3(ubo.uMV) * nM`) so the light direction and
        // the geometry it lights land in the same space, then renormalize (the decomp's own
        // formula produces a non-unit vector before its own normalize() call too).
        float wd[3] = {
            mv16[0] * lightDirOv[0] + mv16[4] * lightDirOv[1] + mv16[8] * lightDirOv[2],
            mv16[1] * lightDirOv[0] + mv16[5] * lightDirOv[1] + mv16[9] * lightDirOv[2],
            mv16[2] * lightDirOv[0] + mv16[6] * lightDirOv[1] + mv16[10] * lightDirOv[2],
        };
        float len = std::sqrt(wd[0] * wd[0] + wd[1] * wd[1] + wd[2] * wd[2]);
        if (len > 1e-6f) {
            wd[0] /= len;
            wd[1] /= len;
            wd[2] /= len;
        }
        base.uLightDir[0] = wd[0];
        base.uLightDir[1] = wd[1];
        base.uLightDir[2] = wd[2];
    } else {
        base.uLightDir[0] = gZelda3dLightDirWorld[0];
        base.uLightDir[1] = gZelda3dLightDirWorld[1];
        base.uLightDir[2] = gZelda3dLightDirWorld[2];
    }
    base.uLightDir[3] = sky ? 1.0f : 0.0f;
    // Light slot 2's world direction for the vertex-lit diffuse sum (#153). Normalized by
    // Zelda3D_UpdateLight; a degenerate (0,0,0) slot direction stays zero and nulls only that
    // light's diffuse term (dot = 0), matching FUN_003fa5d0's semantics.
    base.uLightDir2[0] = gZelda3dLight2Dir[0];
    base.uLightDir2[1] = gZelda3dLight2Dir[1];
    base.uLightDir2[2] = gZelda3dLight2Dir[2];
    base.uLightDir2[3] = 0.0f;
    // §6.3/§6.6's light-env ambient (0.18,0.18,0.18,1); the diffuse coefficient is WHITE and
    // lives directly in the shader's ndotl term. uSheen.x doubles as the wordmark-path gate.
    base.uSheen[0] = lightDirOv ? kWordmarkLightAmbient : 0.0f;
    base.uSheen[1] = base.uSheen[2] = base.uSheen[3] = 0.0f;
    base.uExtra[0] = a8 / 255.0f;
    base.uExtra[1] = uvOffU;
    base.uExtra[2] = uvOffV;
    base.uShadow[0] = 0.0f; // shadow-map/SSAO enhancements removed (OoT3D lighting only)
    base.uFog[0] = gZelda3dFogColor[0];
    base.uFog[1] = gZelda3dFogColor[1];
    base.uFog[2] = gZelda3dFogColor[2];
    base.uFog[3] = gZelda3dFogEnable ? 1.0f : 0.0f;
    base.uFog2[0] = gZelda3dFogMul;
    base.uFog2[1] = gZelda3dFogOffset;
    // OoT3D PICA distance fog (title port): frame-level params; the per-group loop below flips
    // uFog.w to 2.0 for materials whose CMB isFogEnabled byte is set.
    memcpy(base.uFog3d0, gZelda3dFog3d, 4 * sizeof(float));
    memcpy(base.uFog3d1, gZelda3dFog3d + 4, 4 * sizeof(float));
    base.uAmbient[0] = gZelda3dWorldAmbColor[0];
    base.uAmbient[1] = gZelda3dWorldAmbColor[1];
    base.uAmbient[2] = gZelda3dWorldAmbColor[2];
    base.uAmbient[3] = 0.0f;
    bool forceBlend = (a8 < 255);

    bool roomHl = (gZelda3dHlGroup >= 0 && gZelda3dHlGroup < (int)m->groups.size());
    // OoT3D winds its front faces CCW; SDL3 GPU never inverts clip-Y (invertY is always 0 here), so
    // the old `invertY ^ flip` term is dead. Front-face = CCW unless gZelda3dFaceCullFlip is set.
    int frontCW = Zelda3DSg::FrontFaceIsCW(gZelda3dFaceCullFlip) ? 1 : 0;

    // Render-unification effort (kanban #131), Phase 2: route through the unified shader/vertex
    // format instead of the fixed CMB shader above when the bit is set. Falls back to the old path
    // if the unified upload fails (never silently drops the draw).
    bool unified = (gUnifiedRenderer & 1) != 0;
    SgModel* um = unified ? ensureUnifiedUploaded(modelId) : nullptr;
    if (unified && (!um || !um->unifiedVbo))
        unified = false;

    std::vector<DrawGroup> dgs;
    dgs.reserve(m->groups.size());
    int gIdx = -1;
    for (const SgGroup& grp : m->groups) {
        gIdx++;
        if (grp.cull)
            continue;
        if (grp.meshId >= 0 && grp.meshId < 64 && !((midMask >> grp.meshId) & 1ull))
            continue;

        SgUbo ubo = base;
        if (roomHl && gIdx == gZelda3dHlGroup) {
            ubo.uTintSkin[0] = 1.0f;
            ubo.uTintSkin[1] = 0.0f;
            ubo.uTintSkin[2] = 0.0f;
        }
        ubo.uParams[2] = grp.alphaTest ? grp.alphaRef : 0.0f;
        ubo.uParams[3] = grp.polygonOffset;
        // OoT3D PICA distance fog (title port): per-DRAW enable = the frame-level 3DS-fog state
        // AND this material's CMB isFogEnabled byte (fog_mode=5 on the 3DS; the additive/effect
        // materials opt out). uFog.w == 2.0 selects the 3DS LUT path in the shader, overriding
        // the (default-off) F3DEX ramp; sky is excluded shader-side via uLightDir.w.
        if (gZelda3dFog3dOn && grp.fogEnabled) {
            ubo.uFog[3] = 2.0f;
        }
        // combScaleRGB is the CMB material's authored TEV stage-0 RGB scale — always apply when
        // the material asks for it. Only the additive scene-ambient floor (uAmbient.w below) is
        // gated by gZelda3dWorldLit (task #16: at title we skip the synthetic vertex-lit compute
        // but keep the material's static brightness).
        ubo.uExtra[3] = grp.vertexLighting ? grp.combScaleRGB : 1.0f;
        // KNOWN GAP (measured 2026-07-22, per_draw_light_setup.md §6): we emulate ONE texture
        // through ONE TEV stage. Zora's Domain's water/waterfall materials enable texture1 (and
        // sometimes texture2) and run TEV stage 1/2 with color_op = MultiplyThenAdd; those are
        // exactly the surfaces measuring 0.62-0.88 of the oracle, while every single-texture
        // single-stage surface in the same frame is far closer. Fixing "Zora's water is dark and
        // desaturated" means emulating multi-texture + stages 1..5, NOT touching the lighting.
        //
        // CLOSED 2026-07-22 (later): multi-stage TEV landed, and the follow-on "Zora ground 0.79 /
        // walls 0.86 scene-wide deficit" that survived it WAS NOT A RENDERER DEFECT AT ALL. It was a
        // HI-RES TEXTURE PACK ASYMMETRY in the measurement: the oracle frames were captured by a
        // harness predating 7a1dc7e0 (Azahar with no custom textures) while our side, launched from
        // the repo root where textures/ lives, rendered Henriko's 4K pack — whose Zora rock/ground
        // replacements are ~20% darker than the ROM texels. Controlled A/B at one matched camera,
        // only the pack differing: near ground d11 0.811 -> 0.977, rock walls d3 0.853 -> 0.921; and
        // over d3's EXCLUSIVE pixels (those no translucent draw overlays) 1.002. Every opaque scene
        // surface at Zora is at parity vanilla-on-vanilla.
        //   FALSIFIED, do not retry as causes of that deficit: (a) "a decal-layer draw we drop
        //   entirely" — the oracle's 27 scene draws already map 1:1 onto our 21 room + 6 waterfall
        //   groups, and the exclusive-pixel ratios are 0.99-1.00, so there is no missing layer;
        //   (b) "ETC1 mip/LOD selection" and (c) "vertex-colour interpolation" — both would have to
        //   act on the ground draw, which now measures 0.99. The "non-monotonic depth banding"
        //   (0.92/0.69/0.83/0.77/...) that motivated all three was the pack's per-texture darkening
        //   sampled at different distances, not a depth-dependent shading error.
        // tools/tev_mask_ratio.py now HARD-FAILS on a pack asymmetry so this cannot recur.
        // See debug_journal/2026-07-22-zora-ground-deficit-was-texpack-asymmetry.md.
        // OoT3D scene-vertex-lit path (task #16): feed uAmbient.xyz = sceneAmb * matAmb.
        // Per LIGHTDIAG at pinned title cursor=650: grass room groups have
        // matAmb=(1,1,1) matDif=(0,0,0), combScale=2. So the diffuse term contributes
        // nothing for these materials — the entire lit result rides on
        // t * vColor * sceneAmb * combScale. The remaining ground dimness is a
        // SCENE-AMBIENT mismatch (SoH forces midnight → sceneAmb=(0.16,0.14,0.30)
        // blue-heavy), not a shader defect. The Az-matching fix is to use OoT3D's
        // actual title-demo lightSettings values, not to change the shader math. See
        // debug_journal/2026-07-04-title-parity-pinned650.md.
        // forceUnlit (title logo / self-illuminated overlays, ZELDA3D_HANDLE_FORCE_UNLIT): ignore
        // this material's own vertex_lighting flag so the scene ambient never darkens the draw.
        bool ambGroup = (grp.vertexLighting && gZelda3dWorldLit && !forceUnlit);
        ubo.uAmbient[0] = gZelda3dAmbient[0] * grp.matAmbient[0];
        ubo.uAmbient[1] = gZelda3dAmbient[1] * grp.matAmbient[1];
        ubo.uAmbient[2] = gZelda3dAmbient[2] * grp.matAmbient[2];
        // .w = enabled-light count (title_env_lighting.md §10/§11's per-enabled-light ambient sum;
        // see the kFrag comment at its consumer). 0 keeps the ambient path off exactly as before.
        // CHARACTER draws (lit) apply ambient ONCE: oracle vsuni capture at title cs1575
        // (scratch/title_ab/actor_light_uniforms.log) shows the Epona/Link draw with
        // amb0=(0.408,0.408,0.239) but amb1=(0,0,0) — N64 Lights_BindAll semantics (one scene
        // ambient) — while the same frame's terrain draws carry the identical ambient in BOTH
        // slots. The per-slot duplication is a scene-material binding, not a global rule.
        //
        // RE-CONFIRMED 2026-07-22 against the live oracle at Zora's Domain AND Kokiri Forest, four
        // times of day, using per-draw ISOLATION (oot3d-decomp/docs/per_draw_light_setup.md).
        // OoT3D binds exactly TWO configurations per frame and this file implements both:
        //   SCENE draw: both slots bound; dir = world (0,-1,0); light diffuse (0,0,0) in BOTH
        //               slots; ambient = sceneAmbient in BOTH slots  -> the x2 below.
        //   ACTOR draw: slot0 dir=+sunVec dif=light2Col amb=sceneAmbient;
        //               slot1 dir=-sunVec dif=light1Col amb=(0,0,0)  -> the `lit ? 1.0f` below.
        // Falsified there, do NOT re-chase:
        //   - "draws carrying matDif=(1,1,1) are lit differently": a SCENE slot's light diffuse
        //     is ZERO, so matDiffuse cannot contribute to any room draw whatever its value.
        //   - "our light dirs (+-0.702,+-0.702,+-0.117) differ from the oracle's
        //     (+-0.121,+-0.816,-+0.565)": the PICA c80/c83 registers are VIEW space; mapped back
        //     with right*x + up*y - fwd*z they equal ours to 3 decimals at BOTH scenes, and they
        //     track the same dayTime sun rotation the engine already computes.
        ubo.uAmbient[3] = ambGroup ? (lit ? 1.0f : gZelda3dAmbientLightCount) : 0.0f;
        // Per-light diffuse products for the vertex-lit sum (#153): matDiffuse * sceneLightColor.
        // Terrain materials bake matDiffuse=BLACK, so these are zero there and the light sum
        // reduces to the previously-verified ambient-only value.
        for (int k = 0; k < 3; k++) {
            ubo.uLitDif1[k] = grp.matDiffuse[k] * gZelda3dLight1Col[k];
            ubo.uLitDif2[k] = grp.matDiffuse[k] * gZelda3dLight2Col[k];
        }
        ubo.uLitDif1[3] = 0.0f;
        ubo.uLitDif2[3] = 0.0f;
        // PICA200 TEV CONSTANT modulate: for materials whose combiner sources CONSTANT in any
        // stage, publish the selected slot's RGB with .a = 1 so the shader applies it. Materials
        // that never reference CONSTANT (e.g. plain MODULATE(PRIM, TEX0)) leave .a = 0 and the
        // shader skips the multiply — this matches OoT3D's per-material combiner semantics.
        // Per-actor override channel (EnHy Step 2c, TownsfolkBehavior::applyDrawOverrides):
        // if this actor has an override for grp.materialIndex, its RGB replaces the CMB-file
        // default and .a is forced to 1 so the shader applies it (townsfolk clothing colour).
        {
            int ci = grp.combConstIdx & 7;
            if (ci > 5) ci = 0;
            ubo.uMatConst[0] = grp.matConstant[ci][0];
            ubo.uMatConst[1] = grp.matConstant[ci][1];
            ubo.uMatConst[2] = grp.matConstant[ci][2];
            // Only apply the CONSTANT modulation when it's a valid MODULATE colour (non-zero
            // RGB). Some CMB materials list CONSTANT as a stage source but the actual combiner
            // op is REPLACE / ADD (multi-stage full emulation is a documented follow-up); their
            // baked constant is (0,0,0,1) which our MODULATE-only fallback would turn to BLACK.
            // fine_star.cmb is the flagship case: combUsesConst=1 + matConst[0]=(0,0,0,1) is
            // just a stage-source marker, and multiplying by 0 hides every star (task #16).
            bool constBlack = (grp.matConstant[ci][0] < 1e-4f && grp.matConstant[ci][1] < 1e-4f &&
                               grp.matConstant[ci][2] < 1e-4f);
            // uMatConst.a carries the CONSTANT stage's hardware RGB scale (1/2/4) as the apply
            // value; 0 = skip. The shader multiplies by uMatConst.rgb * uMatConst.a so the PICA
            // per-stage scale (fire-glow ×2) rides the same seam.
            ubo.uMatConst[3] = (grp.combUsesConst && !constBlack) ? grp.combConstScaleRGB : 0.0f;
            if (matConstMap && grp.materialIndex >= 0) {
                auto ov = matConstMap->find(grp.materialIndex);
                if (ov != matConstMap->end() && ov->second.constIdx == ci) {
                    ubo.uMatConst[0] = ov->second.rgba[0];
                    ubo.uMatConst[1] = ov->second.rgba[1];
                    ubo.uMatConst[2] = ov->second.rgba[2];
                    ubo.uMatConst[3] = grp.combConstScaleRGB; // force apply (townsfolk clothing colour)
                }
            }
        }

        // Dual-texture stage 0 (fire-glow detail mask): enable flag on uSheen.y, coordinator-1
        // transform (+ this draw's CMAB UV-scroll) on uTex1Xf. The per-draw scroll offset routes
        // to coordinator 1 here (the material the CMAB animates — its Translation track is
        // channelIndex 1, title_logo_fireglow_cmab.md §3.2 fix 3) instead of the coordinator-0
        // offset uExtra.yz used by single-texture scroll consumers (sky cloud band, #28b).
        if (grp.dualTexMode) {
            ubo.uSheen[1] = (float)grp.dualTexMode;
            ubo.uSheen[2] = grp.dualTexScale2;
            ubo.uSheen[3] = (grp.dualTexMode == 4) ? 3.0f : (float)grp.coord1Mapping;
            ubo.uTex1Xf[0] = grp.uv1Scale[0];
            ubo.uTex1Xf[1] = grp.uv1Scale[1];
            ubo.uTex1Xf[2] = grp.uv1Trans[0] + uvOffU;
            ubo.uTex1Xf[3] = grp.uv1Trans[1] + uvOffV;
            ubo.uExtra[1] = 0.0f;
            ubo.uExtra[2] = 0.0f;
        }

        // Generic per-stage TEV chain (render.multi-stage-tev): everything that is neither the
        // trivial single-MODULATE legacy shape nor a classified dual-texture title shape
        // (cmb.cpp parseMats routing). uTevCtl.x > 0 switches the fragment shader to tevRun();
        // the legacy combiner knobs (uExtra.w scale / uMatConst / uSheen.y) are bypassed there.
        if (grp.tevGeneric && grp.tevStageCount > 0) {
            ubo.uTevCtl[0] = (float)grp.tevStageCount;
            ubo.uTevCtl[1] = (float)grp.coord1Mapping;
            ubo.uTevCtl[2] = (float)grp.coord2Mapping;
            for (int s = 0; s < grp.tevStageCount && s < 6; s++) {
                ubo.uTevStages[s * 4 + 0] = grp.tevStagePack[s][0];
                ubo.uTevStages[s * 4 + 1] = grp.tevStagePack[s][1];
                ubo.uTevStages[s * 4 + 2] = grp.tevStagePack[s][2];
                ubo.uTevStages[s * 4 + 3] = 0;
            }
            // Constant-color palette, quantized to RGBA8 (PICA's const registers are 8-bit),
            // AFTER the per-actor override channel (EnHy townsfolk clothing colours patch a
            // slot at draw time — same override data the legacy uMatConst path consumes).
            float constSlots[6][4];
            memcpy(constSlots, grp.matConstant, sizeof(constSlots));
            if (matConstMap && grp.materialIndex >= 0) {
                auto ov = matConstMap->find(grp.materialIndex);
                if (ov != matConstMap->end() && ov->second.constIdx >= 0 && ov->second.constIdx < 6) {
                    for (int k = 0; k < 4; k++)
                        constSlots[ov->second.constIdx][k] = ov->second.rgba[k];
                }
            }
            for (int s = 0; s < 6; s++) {
                auto q = [](float v) {
                    int i = (int)(v * 255.0f + 0.5f);
                    return (uint32_t)(i < 0 ? 0 : (i > 255 ? 255 : i));
                };
                ubo.uTevConst[s] = q(constSlots[s][0]) | (q(constSlots[s][1]) << 8) |
                                   (q(constSlots[s][2]) << 16) | (q(constSlots[s][3]) << 24);
            }
            // Coordinator-1/2 transforms for the extra units. The vertex shader's uv1 sphere
            // path gates on uSheen.w > 2.5 (shared with the dual-tex title path); mapping 4
            // (ProjectionMap) is not emulated and falls back to plain UV.
            ubo.uSheen[3] = (grp.coord1Mapping == 3) ? 3.0f : 1.0f;
            ubo.uTex1Xf[0] = grp.uv1Scale[0];
            ubo.uTex1Xf[1] = grp.uv1Scale[1];
            ubo.uTex1Xf[2] = grp.uv1Trans[0];
            ubo.uTex1Xf[3] = grp.uv1Trans[1];
            ubo.uTex2Xf[0] = grp.uv2Scale[0];
            ubo.uTex2Xf[1] = grp.uv2Scale[1];
            ubo.uTex2Xf[2] = grp.uv2Trans[0];
            ubo.uTex2Xf[3] = grp.uv2Trans[1];
        }

        // Facial material-anim override.
        int texIndex = grp.texIndex;
        if (matTexMap && grp.materialIndex >= 0) {
            auto ov = matTexMap->find(grp.materialIndex);
            if (ov != matTexMap->end() && ov->second >= 0)
                texIndex = ov->second;
        }
        SDL_GPUTexture* tex = Fast::g_activeSdl3GpuApi->DummyTexture();
        SDL_GPUSampler* samp = Fast::g_activeSdl3GpuApi->DummySampler();
        bool additive = isAdditiveBlendGroup(grp);
        if (texIndex >= 0 && texIndex < (int)m->textures.size() && m->textures[texIndex]) {
            tex = m->textures[texIndex];
            samp = getSampler(grp.wrapS, grp.wrapT, additive);
        }
        // Second texture binding: non-dummy when the group's dual-tex mode is on (uSheen.y gates
        // the shader-side sample) or its generic TEV chain can source TEXTURE1. Third binding
        // (uTex2, the repurposed ex-shadow sampler slot): generic TEV only.
        SDL_GPUTexture* tex1 = nullptr;
        SDL_GPUSampler* samp1 = nullptr;
        if ((grp.dualTexMode || grp.tevGeneric) && grp.tex1Index >= 0 &&
            grp.tex1Index < (int)m->textures.size() && m->textures[grp.tex1Index]) {
            tex1 = m->textures[grp.tex1Index];
            samp1 = getSampler(grp.wrap1S, grp.wrap1T, additive);
        }
        SDL_GPUTexture* tex2 = nullptr;
        SDL_GPUSampler* samp2 = nullptr;
        if (grp.tevGeneric && grp.tex2Index >= 0 && grp.tex2Index < (int)m->textures.size() &&
            m->textures[grp.tex2Index]) {
            tex2 = m->textures[grp.tex2Index];
            samp2 = getSampler(grp.wrap2S, grp.wrap2T, additive);
        }

        // Translucent draw over an opaque material: synthesize a standard alpha-over pipeline.
        SgGroup gb = grp;
        if (forceBlend && !grp.blendEnable) {
            gb.blendEnable = 1;
            gb.bSrcRGB = 0x0302;
            gb.bDstRGB = 0x0303;
            gb.bEqRGB = 0x8006;
            gb.bSrcA = 0x0302;
            gb.bDstA = 0x0303;
            gb.bEqA = 0x8006;
        }

        DrawGroup dg;
        dg.tex = tex;
        dg.samp = samp;
        dg.tex1 = tex1;
        dg.samp1 = samp1;
        dg.tex2 = tex2;
        dg.samp2 = samp2;
        dg.first = grp.first;
        dg.count = grp.count;
        if (unified) {
            bool hasTex = tex != Fast::g_activeSdl3GpuApi->DummyTexture();
            auto variant = VariantForGroup(gb, hasTex);
            dg.pipeline = getUnifiedPipeline(gb, frontCW, (int)variant);

            Zelda3DUnified::UnifiedDrawUbo uu{};
            memcpy(uu.common.uMvp, base.uMP, sizeof(uu.common.uMvp));
            memcpy(uu.common.uMv, base.uMV, sizeof(uu.common.uMv));
            memcpy(uu.common.uLightDir, base.uLightDir, sizeof(uu.common.uLightDir));
            // Cycle 0 = texel0 * vColor0 (matches the old fixed shader's `t.rgb * vColor.rgb`); no
            // real per-material TEV data exists on the CMB side yet to derive a richer mux from.
            static const int32_t kCombA[16] = {
                /* cyc0 rgb */ 8 /*TEXEL0*/, 0 /*0*/, 1 /*INPUT_1*/, 0,
                /* cyc0 a   */ 8, 0, 1, 0,
                /* cyc1 rgb */ 0, 0, 0, 0,
                /* cyc1 a   */ 0, 0, 0, 0,
            };
            memcpy(uu.common.uCombA, kCombA, sizeof(uu.common.uCombA));
            uu.common.uPrimColor[0] = uu.common.uPrimColor[1] = uu.common.uPrimColor[2] = uu.common.uPrimColor[3] = 1.0f;
            uu.common.uEnvColor[0] = uu.common.uEnvColor[1] = uu.common.uEnvColor[2] = uu.common.uEnvColor[3] = 0.0f;
            uu.common.uFogColor[0] = base.uFog[0]; uu.common.uFogColor[1] = base.uFog[1]; uu.common.uFogColor[2] = base.uFog[2];
            uu.common.uFogColor[3] = 0.0f;
            uu.common.uParams0[0] = grp.alphaTest ? grp.alphaRef : 0.0f;
            int lightingMode = (grp.vertexLighting && gZelda3dWorldLit && !forceUnlit)
                                    ? 2
                                    : ((lit && !forceUnlit) ? 1 : 0);
            uu.common.uParams0[1] = (float)lightingMode;
            uu.common.uParams0[2] = 1.0f; // cycleCount — CMB never needs the N64 2-cycle shape
            uu.common.uParams0[3] = 0.0f; // frame_count — CMB draws don't use SHADER_NOISE
            uu.common.uParams1[0] = 0.0f; // noise_scale — unused (no SHADER_NOISE on CMB content)
            uu.common.uParams1[1] = grp.polygonOffset;
            uu.common.uParams1[2] = (boneData && boneCnt > 0) ? 1.0f : 0.0f;
            uu.common.uParams1[3] = 0.0f;
            // OoT3D's real vertex-lit formula (docs/oot3d_world_lighting_re.md,
            // cmb.h vertex_lighting header) is:
            //   v_Color = saturate(sceneAmb*matAmbient + sceneDif*matDiffuse*max(0,N.L)) * bakedColor
            // The unified vertex shader computes `lit = uMatAmbient + uMatDiffuse*NdotL`, no
            // separate scene-uniform multiplication — so the scene modulation MUST be pre-baked
            // here at UBO fill. Otherwise sceneAmb / sceneDif are dead data (as they were pre-
            // task-#16 for the unified path), materials with matAmb=(1,1,1) render at full day
            // brightness even at midnight (visible in the title SxS: bright red-brown mountains
            // where the oracle showed near-silhouette). This is the actual port defect; the
            // "environment-value tuning" earlier was chasing a symptom of it.
            uu.common.uMatAmbient[0] = grp.matAmbient[0] * gZelda3dAmbient[0];
            uu.common.uMatAmbient[1] = grp.matAmbient[1] * gZelda3dAmbient[1];
            uu.common.uMatAmbient[2] = grp.matAmbient[2] * gZelda3dAmbient[2];
            uu.common.uMatAmbient[3] = 0.0f;
            uu.common.uMatDiffuse[0] = grp.matDiffuse[0] * gZelda3dLight1Col[0];
            uu.common.uMatDiffuse[1] = grp.matDiffuse[1] * gZelda3dLight1Col[1];
            uu.common.uMatDiffuse[2] = grp.matDiffuse[2] * gZelda3dLight1Col[2];
            uu.common.uMatDiffuse[3] = 0.0f;
            memcpy(uu.bones, base.uBones, sizeof(uu.bones));
            static_assert(sizeof(uu) == sizeof(SgUbo), "UnifiedDrawUbo must match DrawGroup::ubo's byte size");
            memcpy(dg.ubo.data(), &uu, sizeof(uu));
        } else {
            dg.pipeline = getPipeline(gb, frontCW);
            memcpy(dg.ubo.data(), &ubo, sizeof(ubo));
        }
        if (dg.pipeline)
            dgs.push_back(std::move(dg));
    }
    if (dgs.empty())
        return;

    SDL_GPUViewport vp{};
    SDL_Rect sc{};
    api->GetZelda3DViewportScissor(vp, sc);
    SDL_GPUBuffer* vbo = unified ? um->unifiedVbo : m->vbo;
    // Fragment sampler slot 1 (ex-shadow-map): now the generic-TEV third texture unit (uTex2);
    // dummy-backed for draws without one.
    SDL_GPUTexture* dummyTex = Fast::g_activeSdl3GpuApi->DummyTexture();
    SDL_GPUSampler* dummySamp = Fast::g_activeSdl3GpuApi->DummySampler();

    // Append each group as a FIRST-CLASS OP_DRAW in the unified op-list (no callback indirection):
    // each interleaves with the N64 geometry in this fb's render pass and replays through the backend's
    // single fragment-sampler bind path, exactly like an N64 triangle draw. Group order is preserved by
    // sequential append (matters for translucency).
    for (const DrawGroup& g : dgs) {
        const int drawIdx = g_sgDrawIdx++;
        if (gZelda3dSgDrawList) {
            fprintf(stderr, "[Zelda3D_SG] draw %d model=%d first=%u count=%u tex=%p tex1=%p tex2=%p\n", drawIdx,
                    modelId, g.first, g.count, (const void*)g.tex, (const void*)g.tex1, (const void*)g.tex2);
        }
        if (gZelda3dSgDrawOnly >= 0 && drawIdx != gZelda3dSgDrawOnly)
            continue; // draw-isolation probe: everything except the selected group is suppressed
        api->AppendZelda3DModelDraw(g.pipeline, vbo, g.first, g.count, g.ubo.data(), g.tex, g.samp,
                                  g.tex2 ? g.tex2 : dummyTex, g.samp2 ? g.samp2 : dummySamp,
                                  g.tex1, g.samp1, vp, sc);
    }
}

void Fast::Zelda3DRenderer::EndPass() {
    g_ctxValid = false;
    // HARD-WARN rather than render an empty frame in silence. FRAGDBG spent weeks silently inert
    // because its injection anchor had been deleted, and "the probe shows nothing" is
    // indistinguishable from "the thing I am probing contributes nothing" — so an out-of-range
    // selection must say so.
    if (gZelda3dSgDrawOnly >= 0 && g_sgDrawIdx > 0 && gZelda3dSgDrawOnly >= g_sgDrawIdx) {
        fprintf(stderr, "[Zelda3D_SG] DRAWONLY=%d but this frame appended only %d group(s) — probe inert\n",
                gZelda3dSgDrawOnly, g_sgDrawIdx);
    }
    // One-shot, but only once it has something to SHOW: the first frames after launch append zero
    // groups (the scene has not loaded), and an arm-at-launch that self-cleared on one of those
    // printed an empty list and then went quiet — the same silent-inertness failure FRAGDBG had.
    if (gZelda3dSgDrawList && g_sgDrawIdx > 0) {
        fprintf(stderr, "[Zelda3D_SG] draw list end: %d group(s) this frame\n", g_sgDrawIdx);
        gZelda3dSgDrawList = 0;
    }
    g_sgDrawIdx = 0;
}

#endif // ENABLE_SDL3GPU
