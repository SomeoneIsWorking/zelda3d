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
using Fast::DepthDraw;
using Fast::GeomRec;
using Fast::PipeKey;
using Fast::SgGroup;
using Fast::SgModel;
using Fast::Zelda3DRenderer;
using Zelda3DSg::SgUbo;
constexpr uint32_t kSgCommonBytes = Zelda3DSg::kCommonBytes;
constexpr uint32_t kSgBonesBytes = Zelda3DSg::kBonesBytes;

// ---- Shared scene/light/effect globals (owned by zelda3d_gl.cpp, set per frame by zelda3d.c) ----
extern "C" float gZelda3dLightDirWorld[3];
extern "C" int gZelda3dLightEnable;
extern "C" int gZelda3dFaceCull;
extern "C" int gZelda3dFaceCullFlip;
extern "C" int gZelda3dFogEnable;
extern "C" float gZelda3dFogColor[3];
extern "C" float gZelda3dFogMul;
extern "C" float gZelda3dFogOffset;
extern "C" float gZelda3dWorldAmbColor[3];
extern "C" float gZelda3dWorldAmb;
extern "C" int gZelda3dWorldLit;
// Live scene light params fed by z_kankyo via Zelda3D_GL_SetLightParams (zelda3d_gl.cpp).
// Used at UBO fill time to pre-bake scene-modulated matAmbient / matDiffuse for the unified
// vertex-lit shader so it matches OoT3D's real formula sceneAmb*matAmb + sceneDif*matDif*NdotL.
extern "C" float gZelda3dAmbient[3];
extern "C" float gZelda3dLight1Col[3];
extern "C" int gUnifiedRenderer; // render-unification effort (kanban #131): bit 0 = CMB unified
// REPL `sgdump <modelId>`: arm a one-shot per-group render-state dump for the next draw of that model.
extern "C" int g_sgDumpModel = -1;
// ZELDA3D_SG_DUMPTEX=<modelId>: at that model's upload, log each source texture's mean RGBA (catches a
// black/failed texture decode, which uploads happen at scene-load before the REPL can arm sgdump).
static int g_sgDumpTexModel = []() {
    const char* v = getenv("ZELDA3D_SG_DUMPTEX");
    return v ? atoi(v) : -1;
}();
extern "C" int gZelda3dHlGroup;
// Dynamic sun-shadow + screen-space AO tunables (M4), owned by zelda3d_gl.cpp; driven by the REPL
// `shadow`/`ao` + RmlUi Graphics menu. The dispatcher (zelda3d_gl.cpp) resolves the master enables
// (gZelda3dShadowEnable / gZelda3dAoEnable) and gates the Begin*/Draw* calls; we mirror the per-effect
// strength/bias the same way the Vulkan path does.
extern "C" int gZelda3dShadowEnable;
extern "C" int gZelda3dShadowHasFocus;
extern "C" float gZelda3dShadowBias;
extern "C" float gZelda3dShadowStrength;
extern "C" int gZelda3dAoEnable;
extern "C" float gZelda3dAoRadius;
extern "C" float gZelda3dAoStrength;
extern "C" float gZelda3dAoBias;
extern "C" float gZelda3dAoMaxDiff;

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
    "    vec4 uMatConst;\n"
#define SG_UBO_BONES_BODY \
    "    mat4 uBones[" SG_STR(ZELDA3D_GL_MAX_BONES) "];\n"

const char* kVert =
    "#version 450\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aNrm;\n"
    "layout(location=2) in vec2 aUv;\n"
    "layout(location=3) in vec4 aBoneId;\n"
    "layout(location=4) in vec4 aBoneW;\n"
    "layout(location=5) in vec4 aColor;\n"
    "layout(location=0) out vec2 vUv;\n"
    "layout(location=1) out vec4 vColor;\n"
    "layout(location=2) out vec3 vNrmView;\n"
    "layout(location=3) out vec3 vWorld;\n"
    "layout(location=4) out float vFogDist;\n"
    "layout(set=1, binding=0, std140) uniform UBO {\n" SG_UBO_COMMON_BODY "} ubo;\n"
    "layout(set=1, binding=1, std140) uniform UBOBones {\n" SG_UBO_BONES_BODY "} bones;\n"
    "void main() {\n"
    "    vColor = aColor;\n"
    "    vec3 sp, nM;\n"
    "    if (ubo.uTintSkin.w > 0.5) {\n"
    "        vec4 acc = vec4(0.0); nM = vec3(0.0);\n"
    "        for (int i = 0; i < 4; i++) {\n"
    "            acc += aBoneW[i] * (bones.uBones[int(aBoneId[i])] * vec4(aPos, 1.0));\n"
    "            nM  += aBoneW[i] * (mat3(bones.uBones[int(aBoneId[i])]) * aNrm);\n"
    "        }\n"
    "        sp = acc.xyz;\n"
    "    } else { sp = aPos; nM = aNrm; }\n"
    "    vec4 c = ubo.uMP * vec4(sp, 1.0);\n"
    "    vFogDist = c.z / c.w;\n"
    "    c.y *= ubo.uParams.x;\n"
    "    c.z = (c.z + c.w) * 0.5;\n"          // GL clip z [-1,1] -> SDL3 GPU/Vulkan [0,1]
    "    if (ubo.uLightDir.w > 0.5) c.z = c.w;\n" // skybox: pin to far plane
    "    gl_Position = c;\n"
    "    vNrmView = mat3(ubo.uMV) * nM;\n"
    "    vWorld = (ubo.uMV * vec4(sp, 1.0)).xyz;\n"
    "    vUv = vec2(aUv.x + ubo.uExtra.y, 1.0 - aUv.y + ubo.uExtra.z);\n"
    "}\n";

const char* kFrag =
    "#version 450\n"
    "layout(location=0) in vec2 vUv;\n"
    "layout(location=1) in vec4 vColor;\n"
    "layout(location=2) in vec3 vNrmView;\n"
    "layout(location=3) in vec3 vWorld;\n"
    "layout(location=4) in float vFogDist;\n"
    "layout(location=0) out vec4 frag;\n"
    "layout(set=3, binding=0, std140) uniform UBO {\n" SG_UBO_COMMON_BODY "} ubo;\n"
    "layout(set=2, binding=0) uniform sampler2D uTex;\n"
    "layout(set=2, binding=1) uniform sampler2D uShadowMap;\n"
    "float shadowLit() {\n"
    "    vec4 lc = ubo.uLightVP * vec4(vWorld, 1.0);\n"
    "    vec3 p = lc.xyz / lc.w;\n"
    "    p = p * 0.5 + 0.5;\n"
    "    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;\n"
    "    float lit = 0.0;\n"
    "    for (int y = -1; y <= 1; y++)\n"
    "        for (int x = -1; x <= 1; x++) {\n"
    "            float d = texture(uShadowMap, p.xy + vec2(float(x), float(y)) * ubo.uShadow.w).r;\n"
    "            lit += (p.z - ubo.uShadow.y > d) ? 0.0 : 1.0;\n"
    "        }\n"
    "    return lit / 9.0;\n"
    "}\n"
    "void main() {\n"
    "    vec4 t = texture(uTex, vUv);\n"
    "    if (t.a < ubo.uParams.z) discard;\n"
    "    gl_FragDepth = gl_FragCoord.z + ubo.uParams.w;\n"
    "    vec3 shade = ubo.uTintSkin.xyz;\n"
    "    if (ubo.uParams.y > 0.5) {\n"
    "        float hl = dot(normalize(vNrmView), normalize(ubo.uLightDir.xyz)) * 0.5 + 0.5;\n"
    "        shade = ubo.uTintSkin.xyz * (0.55 + 0.45 * hl);\n"
    "    }\n"
    "    if (ubo.uShadow.x > 0.5)\n"
    "        shade *= (1.0 - ubo.uShadow.z * (1.0 - shadowLit()));\n"
    // Character/prop path (uParams.y > 0.5) keeps the shade compound. Scene-lit path
    // (uParams.y < 0.5, ambGroup active via uAmbient.w > 0) skips the shade compound
    // and uses ONLY uAmbient — matches OoT3D's authentic scene-vertex-lit model,
    // saturate(sceneAmb*matAmb + ...) * bakedColor, per debug_journal/
    // 2026-07-04-title-parity-pinned650.md. Prior compound of both shade * uAmbient
    // was ~5.5× too dark at cursor=650 title-demo ground (R6G7B4 vs Az R33G51B27).
    "    bool sceneLitPath = (ubo.uParams.y < 0.5 && ubo.uAmbient.w > 0.0);\n"
    "    vec3 rgb;\n"
    "    if (sceneLitPath) {\n"
    "        rgb = t.rgb * vColor.rgb;\n"                     // skip shade
    "    } else {\n"
    "        rgb = t.rgb * vColor.rgb * shade;\n"             // char/prop keep shade
    "    }\n"
    // PICA200 CONSTANT-color modulation (EnHy townsfolk body color). The apply flag lives in
    // uMatConst.a; when >=0.5 the fragment gets multiplied by uMatConst.rgb (matches the
    // OoT3D combiner MODULATE(current, CONSTANT) that runs after material-anim on townsfolk).
    // Default upload leaves .a=0 so materials that don't reference CONSTANT are unchanged.
    "    if (ubo.uMatConst.a >= 0.5) rgb *= ubo.uMatConst.rgb;\n"
    "    if (ubo.uParams.y < 0.5) {\n"
    "        if (ubo.uAmbient.w > 0.0)\n"
    "            rgb *= ubo.uAmbient.xyz;\n"
    "        rgb = clamp(rgb, 0.0, 1.0) * ubo.uExtra.w;\n"
    "    }\n"
    "    if (ubo.uFog.w > 0.5 && ubo.uLightDir.w < 0.5) {\n"
    "        float f = clamp(vFogDist * ubo.uFog2.x + ubo.uFog2.y, 0.0, 255.0) * (1.0 / 255.0);\n"
    "        rgb = mix(rgb, ubo.uFog.xyz, f);\n"
    "    }\n"
    "    frag = vec4(rgb, t.a * vColor.a * ubo.uExtra.x);\n"
    "}\n";

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

} // namespace

SDL_GPUSampler* Fast::Zelda3DRenderer::getSampler(unsigned wrapS, unsigned wrapT) {
    // Use the backend's single sampler cache. The model path needs LINEAR + max_lod=1000: left at the
    // zero default, a LINEAR-minified large texture (e.g. the 2048² Kokiri ground) computes an LOD > 0
    // that clamps against max_lod=0 and samples nothing -> the surface renders BLACK. Only large/
    // minified textures hit it (small ones stay at LOD 0), which is why the terrain vanished but
    // props/sky did not. max_lod=1000 lands in a distinct cache slot from the N64 samplers (max_lod=0).
    return Fast::g_activeSdl3GpuApi->GetOrCreateSamplerEx(SDL_GPU_FILTER_LINEAR, wrapMode(wrapS), wrapMode(wrapT),
                                                          1000.0f);
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

    // ZELDA3D_SG_FRAGDBG=<1..4>: replace the combiner with an isolated stage so a black/missing draw is
    // diagnosed by VALUE — 1=texture only, 2=vertex colour only, 3=solid white, 4=shade(lighting) only.
    // Applied to EVERY Zelda3D draw unconditionally (unlike a per-draw uniform gate), so the readback is
    // trustworthy. Measure the result with the REPL `region` tool, not by eye.
    std::string fragSrc = kFrag;
    if (const char* dbg = getenv("ZELDA3D_SG_FRAGDBG")) {
        const std::string anchor = "vec3 rgb = t.rgb * vColor.rgb * shade;\n";
        const int mode = atoi(dbg);
        // Return IMMEDIATELY so the override bypasses the later combiner/ambient/FOG stages — otherwise
        // the fog mix (fog colour ~= the scene tan) repaints the probe and hides what we're isolating.
        const char* inject = mode == 1 ? "frag = vec4(t.rgb, 1.0); return;\n"
                             : mode == 2 ? "frag = vec4(vColor.rgb, 1.0); return;\n"
                             : mode == 3 ? "frag = vec4(1.0); return;\n"
                             : mode == 4 ? "frag = vec4(shade, 1.0); return;\n"
                                         : "";
        size_t p = fragSrc.find(anchor);
        if (p != std::string::npos)
            fragSrc.insert(p + anchor.size(), inject);
        fprintf(stderr, "[Zelda3D_SG] FRAGDBG mode=%d active\n", mode);
    }

    std::vector<uint32_t> vsSpv, fsSpv;
    if (!CompileGlsl(EShLangVertex, kVert, vsSpv) || !CompileGlsl(EShLangFragment, fragSrc.c_str(), fsSpv))
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
    fci.num_samplers = 2;        // uTex + uShadowMap
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
        if (modelId == g_sgDumpModel || g_sgDumpTexModel == modelId) {
            if (g_sgDumpTexModel == modelId && texs[i].rgba) {
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
    uint32_t first, count;
    std::array<uint8_t, sizeof(SgUbo)> ubo;
};

// ====================================================================================================
// M4 — dynamic sun-shadows + screen-space AO (ported from zelda3d_vk.cpp onto the unified op model).
//
// The two offscreen depth renders (shadow map from the sun's POV; AO depth from the camera) run as
// AppendZelda3DOwnPass ops — each owns its own SDL3 GPU render pass into private offscreen targets,
// appended BEFORE the visible model in-pass ops. The model fragment shader PCF-samples the shadow
// map; the SSAO composite samples the AO depth as a full-screen in-pass op (multiply-blended) AFTER
// the model draws. Per the P3 plan's gotcha, depth is rendered into an R32_FLOAT *color* target
// (writing gl_FragCoord.z) and sampled as a plain sampler2D `.r`, rather than sampling a D32 depth
// texture — avoiding SDL3 GPU depth-as-sampler pitfalls. A transient D32_FLOAT depth target backs
// each pass's z-test so the nearest fragment wins.
// ----------------------------------------------------------------------------------------------------

constexpr uint32_t kShadowRes = 2048; // square sun-shadow map (P3 plan)
constexpr SDL_GPUTextureFormat kDepthColorFormat = SDL_GPU_TEXTUREFORMAT_R32_FLOAT; // sampled depth
constexpr SDL_GPUTextureFormat kDepthZFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;     // transient z-test

// Depth-only fragment shader: alpha-test discard, then write gl_FragCoord.z into the R32F color.
// The vertex shader (kVert, reused) computes gl_Position identically to the visible draw, so the
// stored depth indexes the shadow PCF / SSAO sample the same as the GL/Vulkan paths.
const char* kDepthFrag =
    "#version 450\n"
    "layout(location=0) in vec2 vUv;\n"
    "layout(location=0) out vec4 outColor;\n"
    "layout(set=3, binding=0, std140) uniform UBO {\n" SG_UBO_COMMON_BODY "} ubo;\n"
    "layout(set=2, binding=0) uniform sampler2D uTex;\n"
    "void main() {\n"
    "    if (texture(uTex, vUv).a < ubo.uParams.z) discard;\n"
    "    outColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);\n"
    "}\n";

// Full-screen triangle (no vertex input) for the SSAO composite.
const char* kAoCompVert =
    "#version 450\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexIndex == 2) ? 3.0 : -1.0, (gl_VertexIndex == 1) ? 3.0 : -1.0);\n"
    "    gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

// SSAO composite: golden-angle spiral over the AO depth, multiply-darken creases (range check
// rejects silhouette edges). Identical math to zelda3d_vk.cpp kAoCompFrag; params via a small UBO.
const char* kAoCompFrag =
    "#version 450\n"
    "layout(location=0) out vec4 frag;\n"
    "layout(set=2, binding=0) uniform sampler2D uAoDepth;\n"
    "layout(set=3, binding=0, std140) uniform PC {\n"
    "    vec4 p0;\n" // xy=texel, z=radius, w=strength
    "    vec4 p1;\n" // x=bias, y=maxDiff
    "} pc;\n"
    "void main() {\n"
    "    vec2 texel = pc.p0.xy; float radius = pc.p0.z; float strength = pc.p0.w;\n"
    "    float bias = pc.p1.x; float maxDiff = pc.p1.y;\n"
    "    vec2 uv = gl_FragCoord.xy * texel;\n"
    "    float d0 = texture(uAoDepth, uv).r;\n"
    "    if (d0 >= 0.99999) { frag = vec4(1.0); return; }\n"
    "    float occ = 0.0;\n"
    "    for (int i = 0; i < 12; i++) {\n"
    "        float a = float(i) * 2.3998277;\n"
    "        float r = radius * (float(i) + 0.5) / 12.0;\n"
    "        vec2 off = vec2(cos(a), sin(a)) * r * texel;\n"
    "        float di = texture(uAoDepth, uv + off).r;\n"
    "        float diff = d0 - di;\n"
    "        if (diff > bias) occ += clamp(1.0 - (diff - bias) / maxDiff, 0.0, 1.0);\n"
    "    }\n"
    "    float ao = 1.0 - strength * (occ / 12.0);\n"
    "    frag = vec4(vec3(ao), 1.0);\n"
    "}\n";

struct AoPush {
    float p0[4]; // xy=texel, z=radius, w=strength
    float p1[4]; // x=bias, y=maxDiff
};

// The DepthDraw record and all M4 module state (g_sgAoResReady, g_depthFrag, g_aoCompVert/Frag,
// g_aoCompPipe, g_depthPipes, g_shadowSampler, g_shadow*/g_ao* targets, g_n64Caster*, g_sgShadowOn,
// g_sgLightVP, the per-pass phase flags + g_shadowDraws/g_aoDraws/g_aoVp/g_aoSc) are now members of
// Fast::Zelda3DRenderer (see fast/backends/zelda3d_sdl3gpu.h).

} // namespace

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

// Depth-only pipeline (R32F color + D32F z-test), reusing the model vertex shader. Keyed on cull.
SDL_GPUGraphicsPipeline* Fast::Zelda3DRenderer::getDepthPipeline(bool doCull, int frontCW) {
    uint32_t key = (doCull ? 2u : 0u) | (doCull && frontCW ? 1u : 0u);
    auto it = g_depthPipes.find(key);
    if (it != g_depthPipes.end())
        return it->second;

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
    pci.fragment_shader = g_depthFrag;
    pci.vertex_input_state.vertex_buffer_descriptions = &vb;
    pci.vertex_input_state.num_vertex_buffers = 1;
    pci.vertex_input_state.vertex_attributes = attrs;
    pci.vertex_input_state.num_vertex_attributes = 6;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = doCull ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face =
        frontCW ? SDL_GPU_FRONTFACE_CLOCKWISE : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.rasterizer_state.enable_depth_clip = false;
    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = true;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pci.depth_stencil_state.enable_stencil_test = false;

    SDL_GPUColorTargetDescription ct{};
    ct.format = kDepthColorFormat;
    ct.blend_state.enable_blend = false;
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = kDepthZFormat;

    SDL_GPUGraphicsPipeline* pipe = SDL_CreateGPUGraphicsPipeline(g_device, &pci);
    if (!pipe)
        fprintf(stderr, "[Zelda3D_SG] depth pipeline create failed: %s\n", SDL_GetError());
    g_depthPipes[key] = pipe;
    return pipe;
}

// One-time M4 resources (shaders, SSAO composite pipeline, shadow sampler). Size-independent; the
// offscreen targets are (re)built by ensureShadowTargets / ensureAoTargets.
bool Fast::Zelda3DRenderer::ensureShadowAoResources() {
    if (g_sgAoResReady)
        return true;
    if (!ensureResources())
        return false;

    g_depthFrag = makeShader(kDepthFrag, EShLangFragment, /*samplers=*/1, /*ubo=*/1);
    g_aoCompVert = makeShader(kAoCompVert, EShLangVertex, 0, 0);
    g_aoCompFrag = makeShader(kAoCompFrag, EShLangFragment, /*samplers=*/1, /*ubo=*/1);
    if (!g_depthFrag || !g_aoCompVert || !g_aoCompFrag) {
        fprintf(stderr, "[Zelda3D_SG] M4 shader create failed: %s\n", SDL_GetError());
        return false;
    }

    // SSAO composite pipeline: full-screen triangle, multiply blend (dst *= src) into the fb colour.
    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = g_aoCompVert;
    pci.fragment_shader = g_aoCompFrag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pci.depth_stencil_state.enable_depth_test = false;
    pci.depth_stencil_state.enable_depth_write = false;
    SDL_GPUColorTargetDescription ct{};
    ct.format = g_activeSdl3GpuApi->GpuColorFormat();
    ct.blend_state.enable_blend = true;
    ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR; // dst' = dst * src
    ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true; // fb 0's pass has a depth attachment bound
    pci.target_info.depth_stencil_format = g_activeSdl3GpuApi->GpuDepthFormat();
    g_aoCompPipe = SDL_CreateGPUGraphicsPipeline(g_device, &pci);
    if (!g_aoCompPipe) {
        fprintf(stderr, "[Zelda3D_SG] AO composite pipeline create failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = SDL_GPU_FILTER_NEAREST;
    si.mag_filter = SDL_GPU_FILTER_NEAREST;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    g_shadowSampler = SDL_CreateGPUSampler(g_device, &si);

    g_sgAoResReady = true;
    fprintf(stderr, "[Zelda3D_SG] M4 shadow+AO resources ready\n");
    return true;
}

SDL_GPUTexture* Fast::Zelda3DRenderer::makeDepthTarget(uint32_t w, uint32_t h, SDL_GPUTextureFormat fmt,
                                                     SDL_GPUTextureUsageFlags usage) {
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = fmt;
    ci.usage = usage;
    ci.width = w;
    ci.height = h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    return SDL_CreateGPUTexture(g_device, &ci);
}

bool Fast::Zelda3DRenderer::ensureShadowTargets(uint32_t dim) {
    if (g_shadowColor && g_shadowDim == dim)
        return true;
    if (g_shadowColor) {
        SDL_WaitForGPUIdle(g_device);
        SDL_ReleaseGPUTexture(g_device, g_shadowColor);
        SDL_ReleaseGPUTexture(g_device, g_shadowZ);
        g_shadowColor = g_shadowZ = nullptr;
    }
    g_shadowColor =
        makeDepthTarget(dim, dim, kDepthColorFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
    g_shadowZ = makeDepthTarget(dim, dim, kDepthZFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
    if (!g_shadowColor || !g_shadowZ)
        return false;
    g_shadowDim = dim;
    fprintf(stderr, "[Zelda3D_SG] shadow map %ux%u ready\n", dim, dim);
    return true;
}

bool Fast::Zelda3DRenderer::ensureAoTargets(uint32_t w, uint32_t h) {
    if (g_aoColor && g_aoW == w && g_aoH == h)
        return true;
    if (g_aoColor) {
        SDL_WaitForGPUIdle(g_device);
        SDL_ReleaseGPUTexture(g_device, g_aoColor);
        SDL_ReleaseGPUTexture(g_device, g_aoZ);
        g_aoColor = g_aoZ = nullptr;
    }
    g_aoColor =
        makeDepthTarget(w, h, kDepthColorFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
    g_aoZ = makeDepthTarget(w, h, kDepthZFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
    if (!g_aoColor || !g_aoZ)
        return false;
    g_aoW = w;
    g_aoH = h;
    fprintf(stderr, "[Zelda3D_SG] AO depth %ux%u ready\n", w, h);
    return true;
}

// Build the per-group depth draws for one model (camera clip for AO, light clip for shadow) and
// append them to `out`. Mirrors zelda3d_vk.cpp recordDepthDraw; the whole model is recorded (the
// hlroom tint only affects the colour pass).
void Fast::Zelda3DRenderer::recordDepthGroups(std::vector<DepthDraw>& out, int modelId, const float* mp16,
                                            const float* mv16, int invertY, float aspectAdj, const float* boneData,
                                            int boneCnt, unsigned long long midMask) {
    SgModel* m = ensureUploaded(modelId);
    if (!m || !m->vbo)
        return;
    SgUbo base{};
    memcpy(base.uMP, mp16, sizeof(base.uMP));
    base.uMP[0] *= aspectAdj;
    base.uMP[4] *= aspectAdj;
    base.uMP[8] *= aspectAdj;
    base.uMP[12] *= aspectAdj;
    memcpy(base.uMV, mv16 ? mv16 : mp16, sizeof(base.uMV));
    for (int k = 0; k < ZELDA3D_GL_MAX_BONES; k++)
        for (int e = 0; e < 16; e++)
            base.uBones[k * 16 + e] = (e % 5 == 0) ? 1.0f : 0.0f;
    if (boneData && boneCnt > 0) {
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
    base.uTintSkin[3] = (boneData && boneCnt > 0) ? 1.0f : 0.0f;
    // OoT3D winds its front faces CCW; SDL3 GPU never inverts clip-Y (invertY is always 0 here), so
    // the old `invertY ^ flip` term is dead. Front-face = CCW unless gZelda3dFaceCullFlip is set.
    int frontCW = Zelda3DSg::FrontFaceIsCW(gZelda3dFaceCullFlip) ? 1 : 0;
    for (const SgGroup& grp : m->groups) {
        if (grp.cull)
            continue;
        if (grp.meshId >= 0 && grp.meshId < 64 && !((midMask >> grp.meshId) & 1ull))
            continue;
        SgUbo ubo = base;
        ubo.uParams[2] = grp.alphaTest ? grp.alphaRef : 0.0f;
        SDL_GPUTexture* tex = Fast::g_activeSdl3GpuApi->DummyTexture();
        SDL_GPUSampler* samp = Fast::g_activeSdl3GpuApi->DummySampler();
        if (grp.texIndex >= 0 && grp.texIndex < (int)m->textures.size() && m->textures[grp.texIndex]) {
            tex = m->textures[grp.texIndex];
            samp = getSampler(grp.wrapS, grp.wrapT);
        }
        bool doCull = grp.faceCull && sgFaceCullOn();
        DepthDraw d;
        d.pipeline = getDepthPipeline(doCull, frontCW);
        d.tex = tex;
        d.samp = samp;
        d.vbo = m->vbo;
        d.first = grp.first;
        d.count = grp.count;
        memcpy(d.ubo.data(), &ubo, sizeof(ubo));
        if (d.pipeline)
            out.push_back(std::move(d));
    }
}

namespace {

// Replay an accumulated depth-draw list into an own offscreen pass (clear colour to 1.0 = far / lit).
// Stateless (takes everything by argument), so it stays a free helper; the EndShadowPass /
// EndDepthPrepass lambdas call it without capturing `this`.
void replayDepthPass(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* color, SDL_GPUTexture* z,
                     const SDL_GPUViewport& vp, const SDL_Rect& sc, const std::vector<DepthDraw>& draws) {
    SDL_GPUColorTargetInfo ct{};
    ct.texture = color;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = SDL_FColor{ 1.0f, 1.0f, 1.0f, 1.0f }; // empty texels read far (1.0): not in shadow / no AO
    SDL_GPUDepthStencilTargetInfo dt{};
    dt.texture = z;
    dt.load_op = SDL_GPU_LOADOP_CLEAR;
    dt.clear_depth = 1.0f;
    dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
    dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
    SDL_SetGPUViewport(pass, &vp);
    SDL_SetGPUScissor(pass, &sc);
    for (const DepthDraw& d : draws) {
        if (!d.pipeline || !d.vbo)
            continue;
        SDL_BindGPUGraphicsPipeline(pass, d.pipeline);
        // Two-block push (see the SG_UBO_COMMON_BODY comment): common state at binding 0 (both stages), bones at vertex
        // binding 1. The depth vertex shader is kVert, so it needs the bone block too.
        SDL_PushGPUVertexUniformData(cmd, 0, d.ubo.data(), kSgCommonBytes);
        SDL_PushGPUFragmentUniformData(cmd, 0, d.ubo.data(), kSgCommonBytes);
        SDL_PushGPUVertexUniformData(cmd, 1, d.ubo.data() + kSgCommonBytes, kSgBonesBytes);
        SDL_GPUBufferBinding vb{};
        vb.buffer = d.vbo;
        vb.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_GPUTextureSamplerBinding sb{};
        sb.texture = d.tex;
        sb.sampler = d.samp;
        SDL_BindGPUFragmentSamplers(pass, 0, &sb, 1);
        SDL_DrawGPUPrimitives(pass, d.count, 1, d.first, 0);
    }
    SDL_EndGPURenderPass(pass);
}

} // namespace

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
                                   const void* matConst) {
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
                     uvOffU, uvOffV, matTex, matConst);
}
extern "C" void Zelda3D_Sg_EndPass(void) {
    if (auto* r = sgRenderer())
        r->EndPass();
}
extern "C" int Zelda3D_Sg_BeginShadowPass(void) {
    if (auto* r = sgRenderer())
        return r->BeginShadowPass();
    return 0;
}
extern "C" void Zelda3D_Sg_ShadowCasterDraw(int modelId, const float* mp16, const float* mv16, const float* boneData,
                                          int boneCnt, unsigned long long midMask) {
    if (auto* r = sgRenderer())
        r->ShadowCasterDraw(modelId, mp16, mv16, boneData, boneCnt, midMask);
}
extern "C" void Zelda3D_Sg_ShadowCasterTris(const float* worldXYZ, size_t triCount, const float* lightVP16) {
    if (auto* r = sgRenderer())
        r->ShadowCasterTris(worldXYZ, triCount, lightVP16);
}
extern "C" void Zelda3D_Sg_EndShadowPass(void) {
    if (auto* r = sgRenderer())
        r->EndShadowPass();
}
extern "C" void Zelda3D_Sg_SetShadow(int on, const float* lightVP16) {
    if (auto* r = sgRenderer())
        r->SetShadow(on, lightVP16);
}
extern "C" int Zelda3D_Sg_BeginDepthPrepass(void) {
    if (auto* r = sgRenderer())
        return r->BeginDepthPrepass();
    return 0;
}
extern "C" void Zelda3D_Sg_DepthPrepassDraw(int modelId, const float* mp16, const float* mv16, int invertY,
                                          float aspectAdj, const float* boneData, int boneCnt,
                                          unsigned long long midMask, int sky) {
    if (auto* r = sgRenderer())
        r->DepthPrepassDraw(modelId, mp16, mv16, invertY, aspectAdj, boneData, boneCnt, midMask, sky);
}
extern "C" void Zelda3D_Sg_EndDepthPrepass(void) {
    if (auto* r = sgRenderer())
        r->EndDepthPrepass();
}
extern "C" void Zelda3D_Sg_AoComposite(void) {
    if (auto* r = sgRenderer())
        r->AoComposite();
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

void Fast::Zelda3DRenderer::BeginPass() {
    g_ctxValid = false;
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
                                    const void* matConst) {
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
        int gi = -1;
        for (const SgGroup& grp : m->groups) {
            gi++;
            const bool hasTex =
                grp.texIndex >= 0 && grp.texIndex < (int)m->textures.size() && m->textures[grp.texIndex];
            fprintf(stderr,
                    "[SG_DUMP]  g%-2d cull=%d faceCull=%d meshId=%d tex=%d%s mat=%d vtxLit=%d combScale=%.3f "
                    "blend=%d(src=%#06x dst=%#06x) aTest=%d aRef=%.3f depthW=%d polyOff=%.4f first=%u count=%u "
                    "vColor0=(%.3f,%.3f,%.3f,%.3f) matAmb=(%.2f,%.2f,%.2f) matDif=(%.2f,%.2f,%.2f) "
                    "uv=[(%.2f,%.2f)(%.2f,%.2f)(%.2f,%.2f)] wrap=(%#06x,%#06x)\n",
                    gi, grp.cull, grp.faceCull, grp.meshId, grp.texIndex, hasTex ? "" : "(MISSING->dummy)",
                    grp.materialIndex, grp.vertexLighting, grp.combScaleRGB, grp.blendEnable, grp.bSrcRGB,
                    grp.bDstRGB, grp.alphaTest, grp.alphaRef, grp.depthWrite, grp.polygonOffset, grp.first,
                    grp.count, grp.dbgColor0[0], grp.dbgColor0[1], grp.dbgColor0[2], grp.dbgColor0[3],
                    grp.matAmbient[0], grp.matAmbient[1], grp.matAmbient[2], grp.matDiffuse[0],
                    grp.matDiffuse[1], grp.matDiffuse[2], grp.dbgUv0[0], grp.dbgUv0[1], grp.dbgUv1[0],
                    grp.dbgUv1[1], grp.dbgUv2[0], grp.dbgUv2[1], grp.wrapS, grp.wrapT);
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
    base.uParams[1] = (lit && gZelda3dLightEnable != 0) ? 1.0f : 0.0f;
    base.uTintSkin[0] = r8 / 255.0f;
    base.uTintSkin[1] = g8 / 255.0f;
    base.uTintSkin[2] = b8 / 255.0f;
    base.uTintSkin[3] = (boneData && boneCnt > 0) ? 1.0f : 0.0f;
    base.uLightDir[0] = gZelda3dLightDirWorld[0];
    base.uLightDir[1] = gZelda3dLightDirWorld[1];
    base.uLightDir[2] = gZelda3dLightDirWorld[2];
    base.uLightDir[3] = sky ? 1.0f : 0.0f;
    base.uExtra[0] = a8 / 255.0f;
    base.uExtra[1] = uvOffU;
    base.uExtra[2] = uvOffV;
    // Dynamic sun-shadow (M4): when on, the model frag PCF-samples the R32F shadow map (bound below).
    if (g_sgShadowOn) {
        base.uShadow[0] = 1.0f;
        base.uShadow[1] = gZelda3dShadowBias;
        base.uShadow[2] = gZelda3dShadowStrength;
        base.uShadow[3] = g_shadowDim ? 1.0f / (float)g_shadowDim : 0.0f;
        memcpy(base.uLightVP, g_sgLightVP, sizeof(base.uLightVP));
    } else {
        base.uShadow[0] = 0.0f;
    }
    base.uFog[0] = gZelda3dFogColor[0];
    base.uFog[1] = gZelda3dFogColor[1];
    base.uFog[2] = gZelda3dFogColor[2];
    base.uFog[3] = gZelda3dFogEnable ? 1.0f : 0.0f;
    base.uFog2[0] = gZelda3dFogMul;
    base.uFog2[1] = gZelda3dFogOffset;
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
        // combScaleRGB is the CMB material's authored TEV stage-0 RGB scale — always apply when
        // the material asks for it. Only the additive scene-ambient floor (uAmbient.w below) is
        // gated by gZelda3dWorldLit (task #16: at title we skip the synthetic vertex-lit compute
        // but keep the material's static brightness).
        ubo.uExtra[3] = grp.vertexLighting ? grp.combScaleRGB : 1.0f;
        // OoT3D scene-vertex-lit path (task #16): feed uAmbient.xyz = sceneAmb * matAmbient so
        // the shader can MULTIPLY (matches OoT3D's saturate(sceneAmb*matAmb + ...) * bakedColor).
        // Only applied to lit scene materials — character/prop draws (uParams.y>0.5) skip it.
        bool ambGroup = (grp.vertexLighting && gZelda3dWorldLit);
        ubo.uAmbient[0] = gZelda3dAmbient[0] * grp.matAmbient[0];
        ubo.uAmbient[1] = gZelda3dAmbient[1] * grp.matAmbient[1];
        ubo.uAmbient[2] = gZelda3dAmbient[2] * grp.matAmbient[2];
        ubo.uAmbient[3] = ambGroup ? 1.0f : 0.0f;
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
            ubo.uMatConst[3] = (grp.combUsesConst && !constBlack) ? 1.0f : 0.0f;
            if (matConstMap && grp.materialIndex >= 0) {
                auto ov = matConstMap->find(grp.materialIndex);
                if (ov != matConstMap->end() && ov->second.constIdx == ci) {
                    ubo.uMatConst[0] = ov->second.rgba[0];
                    ubo.uMatConst[1] = ov->second.rgba[1];
                    ubo.uMatConst[2] = ov->second.rgba[2];
                    ubo.uMatConst[3] = 1.0f; // force apply (townsfolk clothing colour)
                }
            }
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
        if (texIndex >= 0 && texIndex < (int)m->textures.size() && m->textures[texIndex]) {
            tex = m->textures[texIndex];
            samp = getSampler(grp.wrapS, grp.wrapT);
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
            int lightingMode = (grp.vertexLighting && gZelda3dWorldLit) ? 2 : ((lit && gZelda3dLightEnable != 0) ? 1 : 0);
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
    SDL_GPUTexture* shadowTex = (g_sgShadowOn && g_shadowColor) ? g_shadowColor : Fast::g_activeSdl3GpuApi->DummyTexture();
    SDL_GPUSampler* shadowSamp =
        (g_sgShadowOn && g_shadowColor) ? g_shadowSampler : Fast::g_activeSdl3GpuApi->DummySampler();

    // Append each group as a FIRST-CLASS OP_DRAW in the unified op-list (no callback indirection):
    // each interleaves with the N64 geometry in this fb's render pass and replays through the backend's
    // single fragment-sampler bind path, exactly like an N64 triangle draw. Group order is preserved by
    // sequential append (matters for translucency).
    for (const DrawGroup& g : dgs)
        api->AppendZelda3DModelDraw(g.pipeline, vbo, g.first, g.count, g.ubo.data(), g.tex, g.samp, shadowTex, shadowSamp,
                                  vp, sc);
}

void Fast::Zelda3DRenderer::EndPass() {
    g_ctxValid = false;
    g_sgShadowOn = false; // each RenderPass cycle re-establishes the shadow term via SetShadow
    g_sgAoReady = false;
}

// --- Dynamic sun-shadows + screen-space AO (M4) --------------------------------------------------
//
// The depth renders are appended as AppendZelda3DOwnPass ops (own offscreen render pass into R32F
// depth targets), BEFORE the visible model in-pass ops. The model frag samples the shadow map; the
// SSAO composite samples the AO depth as a full-screen in-pass op AFTER the model draws. All three
// replay in the SAME command buffer as the N64 + model ops (SDL3 GPU inserts the write->sample
// barriers automatically), so there is no separate-pass handshake.

int Fast::Zelda3DRenderer::BeginShadowPass() {
    g_shadowDraws.clear();
    g_sgShadowPassActive = false;
    if (!gZelda3dShadowEnable || !gZelda3dShadowHasFocus || !g_activeSdl3GpuApi)
        return 0;
    if (!ensureResources() || !ensureShadowAoResources() || !ensureShadowTargets(kShadowRes))
        return 0;
    g_sgShadowPassActive = true;
    return 1;
}

void Fast::Zelda3DRenderer::ShadowCasterDraw(int modelId, const float* mp16, const float* mv16, const float* boneData,
                                           int boneCnt, unsigned long long midMask) {
    if (!g_sgShadowPassActive)
        return;
    // mp16 = lightVP * (model->world); invertY forced 0 (the model frag samples uLightVP*world directly).
    recordDepthGroups(g_shadowDraws, modelId, mp16, mv16, /*invertY=*/0, /*aspectAdj=*/1.0f, boneData, boneCnt,
                      midMask);
}

void Fast::Zelda3DRenderer::ShadowCasterTris(const float* worldXYZ, size_t triCount, const float* lightVP16) {
    if (!g_sgShadowPassActive || !worldXYZ || triCount == 0)
        return;
    const uint32_t vtxCount = (uint32_t)(triCount * 3);
    const uint32_t bytes = vtxCount * (uint32_t)sizeof(Zelda3DGlVtx);

    // Grow the per-frame caster buffer on demand. Steady-state uploads use cycle=true so a write
    // can't race the previous frame's GPU read of the same buffer.
    if (g_n64CasterCap < bytes) {
        if (g_n64CasterBuf) {
            SDL_WaitForGPUIdle(g_device);
            SDL_ReleaseGPUBuffer(g_device, g_n64CasterBuf);
            g_n64CasterBuf = nullptr;
        }
        uint32_t cap = bytes + bytes / 2 + 4096;
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bci.size = cap;
        g_n64CasterBuf = SDL_CreateGPUBuffer(g_device, &bci);
        g_n64CasterCap = cap;
    }
    if (!g_n64CasterBuf)
        return;

    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = bytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(g_device, &tci);
    Zelda3DGlVtx* dst = (Zelda3DGlVtx*)SDL_MapGPUTransferBuffer(g_device, tb, false);
    for (uint32_t i = 0; i < vtxCount; i++) {
        Zelda3DGlVtx v{};
        v.pos[0] = worldXYZ[i * 3 + 0];
        v.pos[1] = worldXYZ[i * 3 + 1];
        v.pos[2] = worldXYZ[i * 3 + 2];
        dst[i] = v;
    }
    SDL_UnmapGPUTransferBuffer(g_device, tb);
    SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(g_device);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = tb;
    SDL_GPUBufferRegion reg{};
    reg.buffer = g_n64CasterBuf;
    reg.size = bytes;
    SDL_UploadToGPUBuffer(cp, &src, &reg, /*cycle=*/true);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(c);
    SDL_ReleaseGPUTransferBuffer(g_device, tb);

    // Positions are world-space -> clip = lightVP * world; identity model, no skin, no cull.
    SgUbo ubo{};
    memcpy(ubo.uMP, lightVP16, sizeof(ubo.uMP));
    static const float kIdentity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    memcpy(ubo.uMV, kIdentity, sizeof(ubo.uMV));
    for (int k = 0; k < ZELDA3D_GL_MAX_BONES; k++)
        for (int e = 0; e < 16; e++)
            ubo.uBones[k * 16 + e] = (e % 5 == 0) ? 1.0f : 0.0f;
    ubo.uParams[0] = 1.0f;   // invertY off
    ubo.uTintSkin[3] = 0.0f; // no skinning
    DepthDraw d;
    d.pipeline = getDepthPipeline(/*doCull=*/false, /*frontCW=*/0);
    d.tex = Fast::g_activeSdl3GpuApi->DummyTexture();
    d.samp = Fast::g_activeSdl3GpuApi->DummySampler();
    d.vbo = g_n64CasterBuf;
    d.first = 0;
    d.count = vtxCount;
    memcpy(d.ubo.data(), &ubo, sizeof(ubo));
    if (d.pipeline)
        g_shadowDraws.push_back(std::move(d));
}

void Fast::Zelda3DRenderer::EndShadowPass() {
    if (!g_sgShadowPassActive)
        return;
    g_sgShadowPassActive = false;
    if (g_shadowDraws.empty() || !g_activeSdl3GpuApi)
        return;
    uint32_t dim = g_shadowDim;
    SDL_GPUTexture* color = g_shadowColor;
    SDL_GPUTexture* z = g_shadowZ;
    g_activeSdl3GpuApi->AppendZelda3DOwnPass(
        [draws = std::move(g_shadowDraws), color, z, dim](SDL_GPUCommandBuffer* cmd) {
            SDL_GPUViewport vp{ 0.0f, 0.0f, (float)dim, (float)dim, 0.0f, 1.0f };
            SDL_Rect sc{ 0, 0, (int)dim, (int)dim };
            replayDepthPass(cmd, color, z, vp, sc, draws);
        });
    g_shadowDraws.clear();
}

void Fast::Zelda3DRenderer::SetShadow(int on, const float* lightVP16) {
    g_sgShadowOn = (on != 0);
    if (g_sgShadowOn && lightVP16)
        memcpy(g_sgLightVP, lightVP16, sizeof(g_sgLightVP));
}

int Fast::Zelda3DRenderer::BeginDepthPrepass() {
    g_aoDraws.clear();
    g_sgAoPassActive = false;
    if (!gZelda3dAoEnable || !g_activeSdl3GpuApi)
        return 0;
    if (!ensureResources() || !ensureShadowAoResources())
        return 0;
    int w = 0, h = 0;
    g_activeSdl3GpuApi->MainFbSize(w, h);
    if (w <= 0 || h <= 0 || !ensureAoTargets((uint32_t)w, (uint32_t)h))
        return 0;
    // Capture the model viewport/scissor so the AO depth is pixel-aligned with the visible draws.
    g_activeSdl3GpuApi->GetZelda3DViewportScissor(g_aoVp, g_aoSc);
    g_sgAoPassActive = true;
    return 1;
}

void Fast::Zelda3DRenderer::DepthPrepassDraw(int modelId, const float* mp16, const float* mv16, int invertY,
                                           float aspectAdj, const float* boneData, int boneCnt,
                                           unsigned long long midMask, int sky) {
    if (!g_sgAoPassActive || sky)
        return;
    recordDepthGroups(g_aoDraws, modelId, mp16, mv16, invertY, aspectAdj, boneData, boneCnt, midMask);
}

void Fast::Zelda3DRenderer::EndDepthPrepass() {
    if (!g_sgAoPassActive)
        return;
    g_sgAoPassActive = false;
    if (g_aoDraws.empty() || !g_activeSdl3GpuApi)
        return;
    SDL_GPUTexture* color = g_aoColor;
    SDL_GPUTexture* z = g_aoZ;
    SDL_GPUViewport vp = g_aoVp;
    SDL_Rect sc = g_aoSc;
    g_activeSdl3GpuApi->AppendZelda3DOwnPass(
        [draws = std::move(g_aoDraws), color, z, vp, sc](SDL_GPUCommandBuffer* cmd) {
            replayDepthPass(cmd, color, z, vp, sc, draws);
        });
    g_aoDraws.clear();
    g_sgAoReady = true;
}

void Fast::Zelda3DRenderer::AoComposite() {
    if (!g_sgAoReady || !gZelda3dAoEnable || !g_activeSdl3GpuApi || !g_aoColor || g_aoW == 0)
        return;
    SDL_GPUViewport vp{};
    SDL_Rect sc{};
    g_activeSdl3GpuApi->GetZelda3DViewportScissor(vp, sc);
    AoPush push{};
    push.p0[0] = 1.0f / (float)g_aoW;
    push.p0[1] = 1.0f / (float)g_aoH;
    push.p0[2] = gZelda3dAoRadius;
    push.p0[3] = gZelda3dAoStrength;
    push.p1[0] = gZelda3dAoBias;
    push.p1[1] = gZelda3dAoMaxDiff;
    // Append as a first-class fullscreen OP_DRAW into the fb pass (over the scene), through the
    // backend's single bind path — same as the model + HUD draws, no callback indirection.
    g_activeSdl3GpuApi->AppendZelda3DFullscreen(g_aoCompPipe, &push, sizeof(push), g_aoColor, g_shadowSampler, vp, sc);
}

#endif // ENABLE_SDL3GPU
