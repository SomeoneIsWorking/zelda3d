// SoH3D Vulkan render pass. See include/fast/soh3d_vk.h for the role split with soh3d_gl.cpp.
//
// Renders the collected OoT3D draw items (textured / GPU-skinned / half-Lambert-lit, per-group
// blend + depth-write + alpha-test + decal depth-bias + mesh_id visibility) into the Fast3D Vulkan
// backend's current command buffer + render pass, so the 3DS content interleaves depth-correctly
// with the N64 geometry. Mirrors soh3d_gl.cpp's drawOne; the per-item pose interpolation is done by
// the shared SoH3D_GL_RenderPass which calls SoH3D_Vk_DrawModel per item.
//
// Dynamic sun-shadows + screen-space AO (the GL pass's extra offscreen passes) are NOT ported here
// yet — this is the core content. They are a self-contained follow-up.
#ifdef ENABLE_VULKAN

#include "fast/soh3d_vk.h"
#include "fast/backends/gfx_vulkan.h"

#include <vulkan/vulkan.h>
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
#include <mutex>

using Fast::SoH3DVkContext;

// World-space sun direction, owned by soh3d_gl.cpp (set per frame by soh3d.c). C linkage.
extern "C" float gSoH3dLightDirWorld[3];
// Scene-lighting master toggle, owned by the GL pass (soh3d_gl.cpp) and driven by the REPL
// `light` / RmlUi Graphics menu. The GL backend ANDs it with each draw's `lit` flag; mirror that
// here so the toggle works under Vulkan too (#72). Default -1 (uninitialised) reads as ON.
extern "C" int gSoH3dLightEnable;
// Backface culling (shared toggle with the GL backend; see soh3d_gl.cpp). -1 = resolve from
// env SOH3D_FACECULL (default ON). gSoH3dFaceCullFlip flips the front-face winding convention.
extern "C" int gSoH3dFaceCull;
extern "C" int gSoH3dFaceCullFlip;
// Screen-space AO tunables, owned by the GL pass (soh3d_gl.cpp) and driven by the REPL `ao*` /
// RmlUi Graphics menu. Mirror the GL backend so the toggle works under Vulkan too (#72).
extern "C" int gSoH3dAoEnable;     // -1 uninit (resolved by the GL dispatcher), 0 off, 1 on
extern "C" float gSoH3dAoRadius;   // SSAO sample radius in PIXELS
extern "C" float gSoH3dAoStrength; // how dark fully-occluded fragments get (0..1)
extern "C" float gSoH3dAoBias;     // min depth delta to count an occluder
extern "C" float gSoH3dAoMaxDiff;  // depth delta beyond which a neighbour is a silhouette, not a crease
// Dynamic sun-shadow tunables, owned by the GL pass (soh3d_gl.cpp), driven by the REPL `shadow` /
// RmlUi Graphics menu. Mirror the GL backend so the toggle works under Vulkan too (#72). The
// world->light-clip matrix (computeLightVP) is computed by the GL dispatcher and handed to us.
extern "C" int gSoH3dShadowEnable;   // -1 uninit (resolved by the GL dispatcher), 0 off, 1 on
extern "C" int gSoH3dShadowHasFocus; // 0 until soh3d.c sets the focus point (no shadows pre-scene)
// N64/OoT3D F3DEX fog (ported from envCtx.lightSettings + z_play.c gSPFogPosition). Owned by the GL
// dispatcher (soh3d_gl.cpp), set each frame by soh3d.c SoH3D_UpdateLight. Mirror under Vulkan.
extern "C" int gSoH3dFogEnable;       // 0 off, 1 on (DEFAULT OFF, #113 — see soh3d_gl.cpp definition)
extern "C" float gSoH3dFogColor[3];   // env fog colour (0..1)
extern "C" float gSoH3dFogMul;        // F3DEX fog multiplier (s16): fog_z = ndcZ*mul + offset
extern "C" float gSoH3dFogOffset;     // F3DEX fog offset    (s16)
extern "C" float gSoH3dShadowBias;
extern "C" float gSoH3dShadowStrength;
static int vkFaceCullOn() {
    if (gSoH3dFaceCull < 0) {
        const char* e = getenv("SOH3D_FACECULL");
        gSoH3dFaceCull = (e && e[0] == '0') ? 0 : 1; // default ON
    }
    return gSoH3dFaceCull;
}

// Deferred model-cache eviction, defined at file scope below; forward-declared so the in-namespace
// startFrameOnce() can call it.
static void applyPendingEvict();

namespace {

// ---- GLSL -> SPIR-V (glslang; the same toolchain the backend uses) ----
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
        fprintf(stderr, "[SoH3D_VK] shader parse failed: %s\n", shader.getInfoLog());
        return false;
    }
    glslang::TProgram prog;
    prog.addShader(&shader);
    if (!prog.link(msg)) {
        fprintf(stderr, "[SoH3D_VK] shader link failed: %s\n", prog.getInfoLog());
        return false;
    }
    glslang::SpvOptions opt;
    opt.disableOptimizer = true;
    glslang::GlslangToSpv(*prog.getIntermediate(stage), spv, &opt);
    return !spv.empty();
}

const char* kVert = R"(#version 450
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec2 aUv;
layout(location=3) in vec4 aBoneId;
layout(location=4) in vec4 aBoneW;
layout(location=5) in vec4 aColor;
layout(location=0) out vec2 vUv;
layout(location=1) out vec4 vColor;
layout(location=2) out vec3 vNrmView;
layout(location=3) out vec3 vWorld;
layout(location=4) out float vFogDist;
layout(binding=0, std140) uniform UBO {
    mat4 uMP;
    mat4 uMV;
    mat4 uBones[64];
    vec4 uLightDir;  // xyz: world-space sun dir; w: 1 = skybox dome (pin to far plane)
    vec4 uParams;    // x=invertY(+1/-1) y=lit z=alphaRef w=depthOffset
    vec4 uTintSkin;  // xyz=tint w=skin(0/1)
    vec4 uExtra;     // x=per-draw alpha (1=opaque) y=texcoord scroll U z=scroll V (cloud drift, #28b)
    mat4 uLightVP;   // WORLD -> sun light-clip (dynamic shadow); unused when uShadow.x==0
    vec4 uShadow;    // x=shadowOn y=bias z=strength w=texel (1/shadowRes)
    vec4 uFog;       // xyz=env fog colour, w=fog enable
    vec4 uFog2;      // x=fog near (view w), y=fog far
    vec4 uAmbient;   // xyz=env ambient colour, w=additive-floor coef (0 = off / non-scene) (#110)
} ubo;
void main() {
    vColor = aColor;
    vec3 sp, nM;
    if (ubo.uTintSkin.w > 0.5) {
        vec4 acc = vec4(0.0); nM = vec3(0.0);
        for (int i = 0; i < 4; i++) {
            acc += aBoneW[i] * (ubo.uBones[int(aBoneId[i])] * vec4(aPos, 1.0));
            nM  += aBoneW[i] * (mat3(ubo.uBones[int(aBoneId[i])]) * aNrm);
        }
        sp = acc.xyz;
    } else { sp = aPos; nM = aNrm; }
    vec4 c = ubo.uMP * vec4(sp, 1.0);
    // F3DEX fog input = the GL-convention NDC z (clipZ/w in [-1,1]) — the EXACT value the RSP fog
    // stage uses (interpreter.cpp: fog_z = z*winv*mul + offset). Capture it BEFORE the Vulkan z
    // remap below, so the fog curve is identical on both backends regardless of the depth range.
    vFogDist = c.z / c.w;
    // uParams.x carries the backend's clip invertY (interpreter passes GetClipParameters().invertY:
    // -1 on Vulkan, +1 on GL). That single negate IS the Vulkan Y-down flip and matches exactly what
    // the interpreter applies to N64 vertices (interpreter.cpp: y = -y). Do NOT negate again.
    c.y *= ubo.uParams.x;
    c.z = (c.z + c.w) * 0.5;  // GL clip z [-1,1] -> Vulkan [0,1]
    if (ubo.uLightDir.w > 0.5) c.z = c.w; // skybox: pin to far plane (Vulkan far = z/w = 1)
    gl_Position = c;
    vNrmView = mat3(ubo.uMV) * nM; // world-space normal (uMV is model->world; see soh3d_gl.cpp)
    vWorld = (ubo.uMV * vec4(sp, 1.0)).xyz; // world-space position for the shadow projection
    // CMB/PICA UVs are top-origin. Texture SAMPLING maps v=0 -> data row 0 identically in GL and
    // Vulkan (the bottom-left/top-left API difference is framebuffer-only, NOT texture data), so the
    // same 1-v flip GL uses is required here too. (Visible only on detailed texels - face/emblem -
    // not on near-uniform cloth, which is why it looked fine at first.)
    vUv = vec2(aUv.x + ubo.uExtra.y, 1.0 - aUv.y + ubo.uExtra.z); // + per-draw cloud-band drift (#28b)
}
)";

const char* kFrag = R"(#version 450
layout(location=0) in vec2 vUv;
layout(location=1) in vec4 vColor;
layout(location=2) in vec3 vNrmView;
layout(location=3) in vec3 vWorld;
layout(location=4) in float vFogDist;
layout(location=0) out vec4 frag;
layout(binding=0, std140) uniform UBO {
    mat4 uMP;
    mat4 uMV;
    mat4 uBones[64];
    vec4 uLightDir;
    vec4 uParams;
    vec4 uTintSkin;
    vec4 uExtra;
    mat4 uLightVP;
    vec4 uShadow;  // x=on y=bias z=strength w=texel
    vec4 uFog;     // xyz=env fog colour, w=fog enable
    vec4 uFog2;    // x=F3DEX fog mul, y=F3DEX fog offset
    vec4 uAmbient; // xyz=env ambient colour, w=additive-floor coef (0 = off / non-scene) (#110)
} ubo;
layout(binding=1) uniform sampler2D uTex;
layout(binding=2) uniform sampler2D uShadowMap;
// Fraction of this fragment that is LIT (1 = fully lit, 0 = in shadow), 3x3 PCF. The light-clip ->
// depth/texcoord mapping matches the depth render (kVert's c.z [0,1] convert + Vulkan Y-down NDC),
// so p = (lc.xyz/lc.w)*0.5+0.5 indexes both the same as the GL path (soh3d_gl.cpp shadowLit).
float shadowLit() {
    vec4 lc = ubo.uLightVP * vec4(vWorld, 1.0);
    vec3 p = lc.xyz / lc.w;
    p = p * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0; // outside box = lit
    float lit = 0.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) {
            float d = texture(uShadowMap, p.xy + vec2(float(x), float(y)) * ubo.uShadow.w).r;
            lit += (p.z - ubo.uShadow.y > d) ? 0.0 : 1.0;
        }
    return lit / 9.0;
}
void main() {
    vec4 t = texture(uTex, vUv);
    if (t.a < ubo.uParams.z) discard;
    gl_FragDepth = gl_FragCoord.z + ubo.uParams.w; // decal depth bias (polygon offset)
    vec3 shade = ubo.uTintSkin.xyz;
    if (ubo.uParams.y > 0.5) { // half-Lambert form term for characters/props
        float hl = dot(normalize(vNrmView), normalize(ubo.uLightDir.xyz)) * 0.5 + 0.5;
        shade = ubo.uTintSkin.xyz * (0.55 + 0.45 * hl);
    }
    if (ubo.uShadow.x > 0.5) // dynamic sun-shadow: darken by the shadowed fraction (lit + unlit draws)
        shade *= (1.0 - ubo.uShadow.z * (1.0 - shadowLit()));
    // OoT3D stage-0 TEV combiner RGB scale (uExtra.w; 1 for non-scene draws -> no-op, x2 for
    // Kokiri grass). saturate before scaling matches the PICA combiner (clamp then *scale).
    // Scoped to SCENE geometry (uParams.y == 0): characters/props use our half-Lambert form
    // term, a different lighting model than OoT3D's combiner, so don't double them here.
    vec3 rgb = t.rgb * vColor.rgb * shade;
    if (ubo.uParams.y < 0.5)
        rgb = clamp(rgb, 0.0, 1.0) * ubo.uExtra.w;
    // OoT3D additive env-AMBIENT floor (#110, docs/oot3d_world_lighting_re.md). The world frag above
    // is purely multiplicative, so a blue night ambient can't enter a green-dominant grass texture.
    // OoT3D applies the scene ambient additively (render.ts:355 t_FragPriColor += u_SceneAmbient).
    // uAmbient.w is the live-derived coef, set to 0 for non-vertex-lit / non-scene draws so this is a
    // no-op everywhere else. Clamp after, before fog.
    if (ubo.uAmbient.w > 0.0)
        rgb = clamp(rgb + ubo.uAmbient.xyz * ubo.uAmbient.w, 0.0, 1.0);
    // N64/OoT3D F3DEX fog (interpreter.cpp:1850): fog_z = ndcZ*fogMul + fogOffset, clamped to
    // [0,255], used as the blend factor toward the scene fog colour. vFogDist carries the GL-NDC z.
    // Skip the skybox dome (uLightDir.w>0.5 -> it IS the far background). The fogMul/fogOffset come
    // from the live per-scene gSPFogPosition(fogNear,1000), so the curve matches the game exactly
    // (Kokiri fogNear~994 -> near fog-free until the far clip, not the old over-dense world ramp).
    if (ubo.uFog.w > 0.5 && ubo.uLightDir.w < 0.5) {
        float f = clamp(vFogDist * ubo.uFog2.x + ubo.uFog2.y, 0.0, 255.0) * (1.0 / 255.0);
        rgb = mix(rgb, ubo.uFog.xyz, f);
    }
    frag = vec4(rgb, t.a * vColor.a * ubo.uExtra.x); // uExtra.x = per-draw alpha
}
)";

// std140 UBO layout matching the shader block.
struct VkUbo {
    float uMP[16];
    float uMV[16];
    float uBones[64 * 16]; // 64 must match SOH3D_GL_MAX_BONES / shader uBones[64] (Gohma=33 bones, #120)
    float uLightDir[4];
    float uParams[4];
    float uTintSkin[4];
    float uExtra[4];   // x = per-draw alpha (1 = opaque); y/z = texcoord scroll U/V (cloud drift, #28b)
    float uLightVP[16]; // WORLD -> sun light-clip (dynamic shadow); identity when shadows off
    float uShadow[4];   // x=on y=bias z=strength w=texel
    float uFog[4];      // xyz = OoT3D env fog colour, w = fog enable (0/1)
    float uFog2[4];     // x = fog near (view-space w), y = fog far; z,w unused
    float uAmbient[4];  // xyz = OoT3D env ambient colour, w = additive-floor coef (#110)
};

struct VkTex {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct VkGroup {
    uint32_t first = 0, count = 0;
    int texIndex = -1;
    int alphaTest = 0;
    float alphaRef = 0.0f;
    unsigned wrapS = 0x2901, wrapT = 0x2901;
    int blendEnable = 0;
    unsigned bSrcRGB = 0x0302, bDstRGB = 0x0303, bEqRGB = 0x8006;
    unsigned bSrcA = 1, bDstA = 0, bEqA = 0x8006;
    float blendColor[4] = { 0, 0, 0, 1 };
    int depthWrite = 1;
    float polygonOffset = 0.0f;
    int cull = 0;
    int faceCull = 0; // 1 = cull back face (CMB cull byte 1); 0 = double-sided
    int meshId = -1;
    int materialIndex = -1; // CMB material slot (key for the facial tex-override; -1 = none)
    // OoT3D world (scene) combiner port (docs/oot3d_world_lighting_re.md): the per-material
    // stage-0 TEV RGB scale (Kokiri grass = x2), the brightness factor the texture*vColor*tint
    // path dropped. Applied only to vertex-lit scene geometry, gated by REPL `worldlit`.
    int vertexLighting = 0;
    float combScaleRGB = 1.0f;
    float matAmbient[3] = { 1.0f, 1.0f, 1.0f }; // material ambient response (#110: modulates the floor)
    float matDiffuse[3] = { 1.0f, 1.0f, 1.0f }; // material diffuse response (#110: gates the floor)
};

struct VkModel {
    bool uploaded = false, failed = false;
    VkBuffer vbo = VK_NULL_HANDLE;
    VkDeviceMemory vboMem = VK_NULL_HANDLE;
    std::vector<VkGroup> groups;
    std::vector<VkTex> textures;
    // Local-space vertex AABB (geometry-value sweep: a model whose WORLD AABB, this*matrix, is
    // implausibly large/degenerate is a misrendered object — caught from renderer VALUES, not pixels).
    float localMin[3] = { 0, 0, 0 }, localMax[3] = { 0, 0, 0 };
    bool hasBounds = false;
};

// Per-frame geometry capture for `geomscan`: every SoH3D model draw records its world-space AABB
// (local AABB transformed by the model->world matrix). The sweep reads these VALUES to flag
// misrendered geometry (huge/NaN/degenerate extent) directly from the renderer. Double-buffered:
// draws fill g_geomCur; startFrameOnce swaps it to g_geomLast so geomscan reads a complete frame.
struct GeomRec {
    int modelId;
    float wmin[3], wmax[3];
};
std::vector<GeomRec> g_geomCur, g_geomLast;

struct Ring {
    VkBuffer ubo = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize capacity = 0;
    VkDeviceSize offset = 0;
    VkDescriptorPool pool = VK_NULL_HANDLE;
};

// ---- module state ----
SoH3DModelProvider g_provider = nullptr;
std::unordered_map<int, VkModel> g_models;

SoH3DVkContext g_ctx{};
bool g_ctxValid = false;

bool g_resReady = false;
VkDevice g_device = VK_NULL_HANDLE;
VkPhysicalDevice g_phys = VK_NULL_HANDLE;
VkDescriptorSetLayout g_setLayout = VK_NULL_HANDLE;
VkPipelineLayout g_pipeLayout = VK_NULL_HANDLE;
VkShaderModule g_vsMod = VK_NULL_HANDLE, g_fsMod = VK_NULL_HANDLE;
VkRenderPass g_renderPass = VK_NULL_HANDLE;
std::map<std::array<uint32_t, 7>, VkPipeline> g_pipelines; // key: flags + 6 blend params
std::map<uint32_t, VkSampler> g_samplers;                  // key: (wrapS<<16)|wrapT
VkTex g_dummyTex{};
VkSampler g_dummySampler = VK_NULL_HANDLE;
VkDeviceSize g_uboStride = 0;
std::vector<Ring> g_rings; // per frame-in-flight

// Reset the per-frame UBO ring + descriptor pool exactly once per RenderPass cycle. The AO depth
// pre-pass and the main pass both record draws into the same ring; whichever begins first does the
// reset (guarded by this flag), so the prepass's UBO writes are not wiped by the main BeginPass.
bool g_frameStarted = false;

constexpr uint32_t kMaxGroupsPerFrame = 8192; // doubled headroom: AO records a 2nd depth pass of groups

// ---- Screen-space AO (mirror of soh3d_gl.cpp aoPass) ----
// Private depth render pass (depth-only): re-renders the SoH3D content's depth with the SAME camera
// transforms as the visible draw, into a private depth image. A full-screen SSAO triangle then
// samples it inside the main pass and MULTIPLY-blends onto the scene colour. Empty/far texels read
// 1.0 (untouched), so only OoT3D pixels are darkened. See the GL backend for the algorithm rationale.
VkRenderPass g_aoDepthRP = VK_NULL_HANDLE;        // single depth attachment, CLEAR -> READ_ONLY
VkShaderModule g_aoDepthFs = VK_NULL_HANDLE;      // depth-only frag (alpha-test discard only)
std::map<uint32_t, VkPipeline> g_aoDepthPipes;    // key: (doCull<<1)|frontCW
VkShaderModule g_aoCompVs = VK_NULL_HANDLE, g_aoCompFs = VK_NULL_HANDLE;
VkDescriptorSetLayout g_aoCompSetLayout = VK_NULL_HANDLE;
VkPipelineLayout g_aoCompPipeLayout = VK_NULL_HANDLE;
VkPipeline g_aoCompPipe = VK_NULL_HANDLE;
VkDescriptorPool g_aoCompPool = VK_NULL_HANDLE;
VkDescriptorSet g_aoCompSet = VK_NULL_HANDLE;
VkSampler g_aoDepthSampler = VK_NULL_HANDLE;
// Private depth image (sized to cover the viewport extent, incl. any offset).
VkImage g_aoDepthImg = VK_NULL_HANDLE;
VkDeviceMemory g_aoDepthMem = VK_NULL_HANDLE;
VkImageView g_aoDepthView = VK_NULL_HANDLE;
VkFramebuffer g_aoDepthFb = VK_NULL_HANDLE;
uint32_t g_aoW = 0, g_aoH = 0;
bool g_aoResReady = false, g_aoResFailed = false;
bool g_aoPrepassActive = false;
constexpr VkFormat kAoDepthFormat = VK_FORMAT_D32_SFLOAT;

// ---- Dynamic sun-shadow state ----
// Square depth map rendered from the sun direction (reuses g_aoDepthRP — same depth-only render
// pass). The model fragment shader projects each world fragment into light-clip and PCF-samples it.
VkImage g_shadowImg = VK_NULL_HANDLE;
VkDeviceMemory g_shadowMem = VK_NULL_HANDLE;
VkImageView g_shadowView = VK_NULL_HANDLE;
VkFramebuffer g_shadowFb = VK_NULL_HANDLE;
uint32_t g_shadowDim = 0;
constexpr uint32_t kShadowRes = 2048; // matches the GL backend's g_shadowRes
bool g_shadowPassActive = false;
// Per-RenderPass shadow state set by SoH3D_Vk_SetShadow, consumed by SoH3D_Vk_DrawModel.
bool g_shadowOn = false;
float g_shadowLightVP[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
// 1x1 dummy depth texture bound to the model shader's shadow sampler when shadows are off (the
// sampler is statically used, so it must always be valid even though uShadow.x gates the sampling).
VkTex g_dummyDepth{};

// #72: per-frame host-visible vertex buffer holding the N64 opaque caster triangle soup
// (SoH3DGlVtx with only pos set), drawn into the shadow pass by SoH3D_Vk_ShadowCasterTris. One per
// frame-in-flight so a recorded draw's vertices stay alive until that frame's GPU work completes;
// grown on demand. The vertex stride must match the depth pipeline's input (sizeof(SoH3DGlVtx)).
struct N64CasterBuf {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize capacity = 0; // bytes
};
std::vector<N64CasterBuf> g_n64CasterBufs; // per frame-in-flight

// Depth-only fragment shader: only alpha-test discard (no colour attachment in the depth RP). The
// vertex shader (kVert, reused) computes gl_Position identically to the visible draw -> pixel-aligned.
const char* kAoDepthFrag = R"(#version 450
layout(location=0) in vec2 vUv;
layout(binding=0, std140) uniform UBO {
    mat4 uMP; mat4 uMV; mat4 uBones[64];
    vec4 uLightDir; vec4 uParams; vec4 uTintSkin; vec4 uExtra;
} ubo;
layout(binding=1) uniform sampler2D uTex;
void main() {
    if (texture(uTex, vUv).a < ubo.uParams.z) discard;
}
)";

const char* kAoCompVert = R"(#version 450
void main() {
    vec2 p = vec2((gl_VertexIndex == 2) ? 3.0 : -1.0, (gl_VertexIndex == 1) ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

// SSAO composite: golden-angle spiral over the private depth, multiply-darken creases (range check
// rejects silhouette edges). Identical math to soh3d_gl.cpp kAoFrag; params via push constants.
const char* kAoCompFrag = R"(#version 450
layout(location=0) out vec4 frag;
layout(binding=0) uniform sampler2D uAoDepth;
layout(push_constant) uniform PC {
    vec2 uTexel; float uRadius; float uStrength; float uBias; float uMaxDiff;
} pc;
void main() {
    vec2 uv = gl_FragCoord.xy * pc.uTexel;
    float d0 = texture(uAoDepth, uv).r;
    if (d0 >= 0.99999) { frag = vec4(1.0); return; } // no SoH3D content here -> no AO
    float occ = 0.0;
    for (int i = 0; i < 12; i++) {
        float a = float(i) * 2.3998277;            // golden-angle spiral
        float r = pc.uRadius * (float(i) + 0.5) / 12.0;
        vec2 off = vec2(cos(a), sin(a)) * r * pc.uTexel;
        float di = texture(uAoDepth, uv + off).r;
        float diff = d0 - di;                       // >0 => neighbour closer to camera
        if (diff > pc.uBias)
            occ += clamp(1.0 - (diff - pc.uBias) / pc.uMaxDiff, 0.0, 1.0);
    }
    float ao = 1.0 - pc.uStrength * (occ / 12.0);
    frag = vec4(vec3(ao), 1.0);
}
)";

struct AoPush {
    float texel[2];
    float radius, strength, bias, maxDiff;
};

uint32_t findMemType(uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(g_phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return 0;
}

void makeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& buf,
                VkDeviceMemory& mem, void** mappedOut) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(g_device, &bi, nullptr, &buf);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_device, buf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemType(req.memoryTypeBits, props);
    vkAllocateMemory(g_device, &ai, nullptr, &mem);
    vkBindBufferMemory(g_device, buf, mem, 0);
    if (mappedOut)
        vkMapMemory(g_device, mem, 0, size, 0, mappedOut);
}

// Run a one-shot transfer command and wait. (Models upload once; not perf-critical.)
template <typename F> void oneShot(F record) {
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = g_ctx.commandPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(g_device, &cai, &cmd);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    record(cmd);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VkFence fence;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(g_device, &fi, nullptr, &fence);
    vkQueueSubmit(g_ctx.graphicsQueue, 1, &si, fence);
    vkWaitForFences(g_device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(g_device, fence, nullptr);
    vkFreeCommandBuffers(g_device, g_ctx.commandPool, 1, &cmd);
}

void uploadTexture(VkTex& t, int w, int h, const unsigned char* rgba) {
    if (w <= 0 || h <= 0)
        w = h = 1;
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = { (uint32_t)w, (uint32_t)h, 1 };
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(g_device, &ii, nullptr, &t.image);
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_device, t.image, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(g_device, &ai, nullptr, &t.mem);
    vkBindImageMemory(g_device, t.image, t.mem, 0);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = t.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCreateImageView(g_device, &vi, nullptr, &t.view);

    const VkDeviceSize size = (VkDeviceSize)w * h * 4;
    static const unsigned char white[4] = { 255, 255, 255, 255 };
    const unsigned char* src = rgba ? rgba : white;
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    void* mapped = nullptr;
    makeBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMem,
               &mapped);
    memcpy(mapped, src, rgba ? size : 4);
    vkUnmapMemory(g_device, stagingMem);

    oneShot([&](VkCommandBuffer cmd) {
        auto barrier = [&](VkImageLayout o, VkImageLayout n, VkAccessFlags sa, VkAccessFlags da,
                           VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = o;
            b.newLayout = n;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = t.image;
            b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            b.srcAccessMask = sa;
            b.dstAccessMask = da;
            vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
        };
        barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
        vkCmdCopyBufferToImage(cmd, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });
    vkDestroyBuffer(g_device, staging, nullptr);
    vkFreeMemory(g_device, stagingMem, nullptr);
}

VkSamplerAddressMode wrapMode(unsigned glWrap) {
    switch (glWrap) {
        case 0x2900: // GL_CLAMP
        case 0x812F: // GL_CLAMP_TO_EDGE
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case 0x8370: // GL_MIRRORED_REPEAT
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        default: // 0x2901 GL_REPEAT
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

VkSampler getSampler(unsigned wrapS, unsigned wrapT) {
    uint32_t key = (wrapS << 16) | (wrapT & 0xFFFF);
    auto it = g_samplers.find(key);
    if (it != g_samplers.end())
        return it->second;
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = wrapMode(wrapS);
    si.addressModeV = wrapMode(wrapT);
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.maxLod = 0.0f;
    VkSampler s;
    vkCreateSampler(g_device, &si, nullptr, &s);
    g_samplers[key] = s;
    return s;
}

VkBlendFactor mapFactor(unsigned f) {
    switch (f) {
        case 0: return VK_BLEND_FACTOR_ZERO;
        case 1: return VK_BLEND_FACTOR_ONE;
        case 0x300: return VK_BLEND_FACTOR_SRC_COLOR;
        case 0x301: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 0x302: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 0x303: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 0x304: return VK_BLEND_FACTOR_DST_ALPHA;
        case 0x305: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 0x306: return VK_BLEND_FACTOR_DST_COLOR;
        case 0x307: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case 0x308: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case 0x8001: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case 0x8002: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case 0x8003: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case 0x8004: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        default: return VK_BLEND_FACTOR_ONE;
    }
}
VkBlendOp mapEq(unsigned e) {
    switch (e) {
        case 0x8006: return VK_BLEND_OP_ADD;
        case 0x800A: return VK_BLEND_OP_SUBTRACT;
        case 0x800B: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case 0x8007: return VK_BLEND_OP_MIN;
        case 0x8008: return VK_BLEND_OP_MAX;
        default: return VK_BLEND_OP_ADD;
    }
}

VkShaderModule makeModule(const std::vector<uint32_t>& spv) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spv.size() * sizeof(uint32_t);
    ci.pCode = spv.data();
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(g_device, &ci, nullptr, &m);
    return m;
}

bool ensureResources(const SoH3DVkContext& ctx) {
    if (g_resReady)
        return true;
    g_device = ctx.device;
    g_phys = ctx.physicalDevice;
    g_renderPass = ctx.renderPass;

    std::vector<uint32_t> vsSpv, fsSpv;
    if (!CompileGlsl(EShLangVertex, kVert, vsSpv) || !CompileGlsl(EShLangFragment, kFrag, fsSpv))
        return false;
    g_vsMod = makeModule(vsSpv);
    g_fsMod = makeModule(fsSpv);

    VkDescriptorSetLayoutBinding binds[3]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[2].binding = 2; // shadow map (dynamic sun-shadow); dummy depth when shadows off
    binds[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[2].descriptorCount = 1;
    binds[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dli{};
    dli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dli.bindingCount = 3;
    dli.pBindings = binds;
    vkCreateDescriptorSetLayout(g_device, &dli, nullptr, &g_setLayout);

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &g_setLayout;
    vkCreatePipelineLayout(g_device, &pli, nullptr, &g_pipeLayout);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(g_phys, &props);
    VkDeviceSize align = props.limits.minUniformBufferOffsetAlignment;
    if (align == 0)
        align = 1;
    g_uboStride = ((sizeof(VkUbo) + align - 1) / align) * align;

    g_rings.resize(ctx.framesInFlight);
    for (uint32_t i = 0; i < ctx.framesInFlight; i++) {
        Ring& r = g_rings[i];
        r.capacity = g_uboStride * kMaxGroupsPerFrame;
        makeBuffer(r.capacity, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, r.ubo, r.mem,
                   &r.mapped);
        VkDescriptorPoolSize ps[2]{};
        ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ps[0].descriptorCount = kMaxGroupsPerFrame;
        ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps[1].descriptorCount = kMaxGroupsPerFrame * 2; // model draws use 2 samplers (tex + shadow)
        VkDescriptorPoolCreateInfo dpi{};
        dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpi.maxSets = kMaxGroupsPerFrame;
        dpi.poolSizeCount = 2;
        dpi.pPoolSizes = ps;
        vkCreateDescriptorPool(g_device, &dpi, nullptr, &r.pool);
    }

    uploadTexture(g_dummyTex, 1, 1, nullptr); // 1x1 white for untextured groups
    g_dummySampler = getSampler(0x2901, 0x2901);

    // 1x1 dummy depth image for the model shader's shadow sampler when shadows are off. Never
    // sampled meaningfully (uShadow.x gates it), but must be a valid bound resource in READ layout.
    {
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = kAoDepthFormat;
        ii.extent = { 1, 1, 1 };
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCreateImage(g_device, &ii, nullptr, &g_dummyDepth.image);
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(g_device, g_dummyDepth.image, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = findMemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(g_device, &ai, nullptr, &g_dummyDepth.mem);
        vkBindImageMemory(g_device, g_dummyDepth.image, g_dummyDepth.mem, 0);
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = g_dummyDepth.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = kAoDepthFormat;
        vi.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        vkCreateImageView(g_device, &vi, nullptr, &g_dummyDepth.view);
        oneShot([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = g_dummyDepth.image;
            b.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &b);
        });
    }

    // Nearest/clamp sampler for the AO depth + shadow map (shared; created here so shadows work
    // even when AO is off). PCF in the shader samples explicit offsets, so NEAREST is correct.
    {
        VkSamplerCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.maxLod = 0.0f;
        vkCreateSampler(g_device, &sci, nullptr, &g_aoDepthSampler);
    }

    g_resReady = true;
    fprintf(stderr, "[SoH3D_VK] resources ready (ubo stride %llu)\n", (unsigned long long)g_uboStride);
    return true;
}

VkPipeline getPipeline(const VkGroup& g, int frontCW) {
    // Backface cull is baked into the pipeline (cullMode/frontFace are not dynamic here), so the
    // cull intent + the winding (which flips with invertY, carried in frontCW) join the key.
    bool doCull = g.faceCull && vkFaceCullOn();
    std::array<uint32_t, 7> key = { (uint32_t)((g.blendEnable ? 1u : 0u) | (g.depthWrite ? 2u : 0u) |
                                               (doCull ? 4u : 0u) | (doCull && frontCW ? 8u : 0u)),
                                    g.bSrcRGB, g.bDstRGB, g.bEqRGB, g.bSrcA, g.bDstA, g.bEqA };
    auto it = g_pipelines.find(key);
    if (it != g_pipelines.end())
        return it->second;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = g_vsMod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = g_fsMod;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(SoH3DGlVtx);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[6]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(SoH3DGlVtx, pos) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(SoH3DGlVtx, nrm) };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)offsetof(SoH3DGlVtx, uv) };
    attrs[3] = { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(SoH3DGlVtx, boneIds) };
    attrs[4] = { 4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(SoH3DGlVtx, weights) };
    attrs[5] = { 5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(SoH3DGlVtx, color) };
    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = 6;
    vin.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable = VK_TRUE; // device feature is on; matches the Fast3D backend
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    // Honor the CMB material cull byte (1 = cull back). The asset winds front faces CCW from the
    // geometric normal; the vertex shader negates clip.y when invertY, flipping window winding ->
    // frontCW carries that (plus the gSoH3dFaceCullFlip convention toggle). Double-sided groups
    // (faceCull 0 / cull disabled) keep VK_CULL_MODE_NONE.
    rs.cullMode = doCull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rs.frontFace = frontCW ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = g.depthWrite ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = g.blendEnable ? VK_TRUE : VK_FALSE;
    cba.srcColorBlendFactor = mapFactor(g.bSrcRGB);
    cba.dstColorBlendFactor = mapFactor(g.bDstRGB);
    cba.colorBlendOp = mapEq(g.bEqRGB);
    cba.srcAlphaBlendFactor = mapFactor(g.bSrcA);
    cba.dstAlphaBlendFactor = mapFactor(g.bDstA);
    cba.alphaBlendOp = mapEq(g.bEqA);
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS };
    VkPipelineDynamicStateCreateInfo dynS{};
    dynS.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynS.dynamicStateCount = 3;
    dynS.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vin;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dynS;
    pci.layout = g_pipeLayout;
    pci.renderPass = g_renderPass;
    pci.subpass = 0;
    VkPipeline pipe = VK_NULL_HANDLE;
    vkCreateGraphicsPipelines(g_device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
    g_pipelines[key] = pipe;
    return pipe;
}

VkModel* ensureUploaded(int modelId) {
    VkModel& m = g_models[modelId];
    if (m.uploaded)
        return &m;
    if (m.failed)
        return nullptr;
    const SoH3DGlGroup* groups = nullptr;
    const SoH3DGlTex* texs = nullptr;
    int groupCount = 0, texCount = 0;
    if (!g_provider || !g_provider(modelId, &groups, &groupCount, &texs, &texCount) || groupCount <= 0) {
        fprintf(stderr, "[SoH3D_VK] model %d unavailable from provider\n", modelId);
        m.failed = true;
        return nullptr;
    }

    std::vector<SoH3DGlVtx> all;
    for (int i = 0; i < groupCount; i++) {
        VkGroup g;
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
        for (int k = 0; k < 4; k++)
            g.blendColor[k] = groups[i].blendColor[k];
        all.insert(all.end(), groups[i].verts, groups[i].verts + groups[i].vertCount);
        m.groups.push_back(g);
    }

    // Local-space vertex AABB for the geometry-value sweep (geomscan).
    if (!all.empty()) {
        for (int k = 0; k < 3; k++) m.localMin[k] = m.localMax[k] = all[0].pos[k];
        for (const SoH3DGlVtx& v : all)
            for (int k = 0; k < 3; k++) {
                if (v.pos[k] < m.localMin[k]) m.localMin[k] = v.pos[k];
                if (v.pos[k] > m.localMax[k]) m.localMax[k] = v.pos[k];
            }
        m.hasBounds = true;
    }

    // Device-local vertex buffer via a staging copy.
    const VkDeviceSize vbBytes = all.size() * sizeof(SoH3DGlVtx);
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    void* mapped = nullptr;
    makeBuffer(vbBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMem,
               &mapped);
    memcpy(mapped, all.data(), vbBytes);
    vkUnmapMemory(g_device, stagingMem);
    makeBuffer(vbBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m.vbo, m.vboMem, nullptr);
    oneShot([&](VkCommandBuffer cmd) {
        VkBufferCopy c{};
        c.size = vbBytes;
        vkCmdCopyBuffer(cmd, staging, m.vbo, 1, &c);
    });
    vkDestroyBuffer(g_device, staging, nullptr);
    vkFreeMemory(g_device, stagingMem, nullptr);

    for (int i = 0; i < texCount; i++) {
        VkTex t;
        uploadTexture(t, texs[i].w, texs[i].h, texs[i].rgba);
        m.textures.push_back(t);
    }
    m.uploaded = true;
    fprintf(stderr, "[SoH3D_VK] uploaded model %d: %d groups, %d textures, %zu verts\n", modelId, groupCount,
            texCount, all.size());
    return &m;
}

// Fill the model vertex-input state (6 attrs) into the supplied structs. Shared by the model and
// AO-depth pipelines (same vertex layout).
void fillVertexInput(VkPipelineVertexInputStateCreateInfo& vin, VkVertexInputBindingDescription& binding,
                     VkVertexInputAttributeDescription attrs[6]) {
    binding = {};
    binding.binding = 0;
    binding.stride = sizeof(SoH3DGlVtx);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(SoH3DGlVtx, pos) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(SoH3DGlVtx, nrm) };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)offsetof(SoH3DGlVtx, uv) };
    attrs[3] = { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(SoH3DGlVtx, boneIds) };
    attrs[4] = { 4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(SoH3DGlVtx, weights) };
    attrs[5] = { 5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(SoH3DGlVtx, color) };
    vin = {};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = 6;
    vin.pVertexAttributeDescriptions = attrs;
}

// One-time AO pipelines/shaders/render-pass/sampler (size-independent). The per-frame depth image
// is built/resized separately by ensureAoDepth.
bool ensureAoResources() {
    if (g_aoResReady)
        return true;
    if (g_aoResFailed)
        return false;

    // --- Depth-only render pass: clear -> store, finalLayout readable in a later fragment shader. ---
    VkAttachmentDescription da{};
    da.format = kAoDepthFormat;
    da.samples = VK_SAMPLE_COUNT_1_BIT;
    da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    da.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    da.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    da.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    da.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference dref{};
    dref.attachment = 0;
    dref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.pDepthStencilAttachment = &dref;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &da;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;
    if (vkCreateRenderPass(g_device, &rpci, nullptr, &g_aoDepthRP) != VK_SUCCESS) {
        g_aoResFailed = true;
        return false;
    }

    // --- Shaders ---
    std::vector<uint32_t> dfs, cvs, cfs;
    if (!CompileGlsl(EShLangFragment, kAoDepthFrag, dfs) || !CompileGlsl(EShLangVertex, kAoCompVert, cvs) ||
        !CompileGlsl(EShLangFragment, kAoCompFrag, cfs)) {
        g_aoResFailed = true;
        return false;
    }
    g_aoDepthFs = makeModule(dfs);
    g_aoCompVs = makeModule(cvs);
    g_aoCompFs = makeModule(cfs);

    // --- Composite descriptor set layout (one combined image sampler) + push-constant layout. ---
    VkDescriptorSetLayoutBinding cb{};
    cb.binding = 0;
    cb.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    cb.descriptorCount = 1;
    cb.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo cli{};
    cli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    cli.bindingCount = 1;
    cli.pBindings = &cb;
    vkCreateDescriptorSetLayout(g_device, &cli, nullptr, &g_aoCompSetLayout);
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(AoPush);
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &g_aoCompSetLayout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(g_device, &pli, nullptr, &g_aoCompPipeLayout);

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    vkCreateDescriptorPool(g_device, &dpi, nullptr, &g_aoCompPool);

    // --- Composite pipeline (full-screen triangle, no vertex input, multiply blend). ---
    VkPipelineShaderStageCreateInfo cst[2]{};
    cst[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cst[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    cst[0].module = g_aoCompVs;
    cst[0].pName = "main";
    cst[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cst[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    cst[1].module = g_aoCompFs;
    cst[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo cvin{};
    cvin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo cia{};
    cia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    cia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo cvp{};
    cvp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    cvp.viewportCount = 1;
    cvp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo crs{};
    crs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    crs.polygonMode = VK_POLYGON_MODE_FILL;
    crs.cullMode = VK_CULL_MODE_NONE;
    crs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    crs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo cms{};
    cms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    cms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo cds{};
    cds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    cds.depthTestEnable = VK_FALSE;
    cds.depthWriteEnable = VK_FALSE;
    cds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState ccba{};
    ccba.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    ccba.blendEnable = VK_TRUE;
    // Multiply: scene *= ao. Matches the GL glBlendFunc(GL_ZERO, GL_SRC_COLOR): dst' = src*0 + dst*src.
    ccba.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    ccba.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
    ccba.colorBlendOp = VK_BLEND_OP_ADD;
    ccba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    ccba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ccba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo ccb{};
    ccb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ccb.attachmentCount = 1;
    ccb.pAttachments = &ccba;
    VkDynamicState cdyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo cdynS{};
    cdynS.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    cdynS.dynamicStateCount = 2;
    cdynS.pDynamicStates = cdyn;
    VkGraphicsPipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    cpci.stageCount = 2;
    cpci.pStages = cst;
    cpci.pVertexInputState = &cvin;
    cpci.pInputAssemblyState = &cia;
    cpci.pViewportState = &cvp;
    cpci.pRasterizationState = &crs;
    cpci.pMultisampleState = &cms;
    cpci.pDepthStencilState = &cds;
    cpci.pColorBlendState = &ccb;
    cpci.pDynamicState = &cdynS;
    cpci.layout = g_aoCompPipeLayout;
    cpci.renderPass = g_renderPass; // the FB render pass (composite draws inside the main pass)
    cpci.subpass = 0;
    if (vkCreateGraphicsPipelines(g_device, VK_NULL_HANDLE, 1, &cpci, nullptr, &g_aoCompPipe) != VK_SUCCESS) {
        g_aoResFailed = true;
        return false;
    }

    g_aoResReady = true;
    fprintf(stderr, "[SoH3D_VK] AO resources ready\n");
    return true;
}

// Depth-only pipeline for the AO pre-pass, keyed on cull state (mirrors getPipeline's cull logic).
VkPipeline getAoDepthPipeline(bool doCull, int frontCW) {
    uint32_t key = (doCull ? 2u : 0u) | (doCull && frontCW ? 1u : 0u);
    auto it = g_aoDepthPipes.find(key);
    if (it != g_aoDepthPipes.end())
        return it->second;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = g_vsMod; // reuse the model vertex shader (same gl_Position math)
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = g_aoDepthFs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    VkVertexInputAttributeDescription attrs[6]{};
    VkPipelineVertexInputStateCreateInfo vin{};
    fillVertexInput(vin, binding, attrs);

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable = VK_TRUE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = doCull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rs.frontFace = frontCW ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 0; // depth-only RP has no colour attachment
    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynS{};
    dynS.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynS.dynamicStateCount = 2;
    dynS.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vin;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dynS;
    pci.layout = g_pipeLayout; // same UBO+sampler set layout as the model pipeline
    pci.renderPass = g_aoDepthRP;
    pci.subpass = 0;
    VkPipeline pipe = VK_NULL_HANDLE;
    vkCreateGraphicsPipelines(g_device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
    g_aoDepthPipes[key] = pipe;
    return pipe;
}

// (Re)create the private depth image + framebuffer sized to cover the viewport extent. Points the
// composite descriptor set at the new view. Waits for idle on resize (the old image may be in use).
bool ensureAoDepth(uint32_t w, uint32_t h) {
    if (w == 0 || h == 0)
        return false;
    if (g_aoDepthImg != VK_NULL_HANDLE && g_aoW == w && g_aoH == h)
        return true;
    if (g_aoDepthImg != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(g_device);
        vkDestroyFramebuffer(g_device, g_aoDepthFb, nullptr);
        vkDestroyImageView(g_device, g_aoDepthView, nullptr);
        vkDestroyImage(g_device, g_aoDepthImg, nullptr);
        vkFreeMemory(g_device, g_aoDepthMem, nullptr);
        g_aoDepthFb = VK_NULL_HANDLE;
        g_aoDepthView = VK_NULL_HANDLE;
        g_aoDepthImg = VK_NULL_HANDLE;
        g_aoDepthMem = VK_NULL_HANDLE;
    }
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = kAoDepthFormat;
    ii.extent = { w, h, 1 };
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(g_device, &ii, nullptr, &g_aoDepthImg) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_device, g_aoDepthImg, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(g_device, &ai, nullptr, &g_aoDepthMem);
    vkBindImageMemory(g_device, g_aoDepthImg, g_aoDepthMem, 0);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = g_aoDepthImg;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = kAoDepthFormat;
    vi.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    vkCreateImageView(g_device, &vi, nullptr, &g_aoDepthView);

    VkFramebufferCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = g_aoDepthRP;
    fci.attachmentCount = 1;
    fci.pAttachments = &g_aoDepthView;
    fci.width = w;
    fci.height = h;
    fci.layers = 1;
    vkCreateFramebuffer(g_device, &fci, nullptr, &g_aoDepthFb);

    if (g_aoCompSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo dai{};
        dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool = g_aoCompPool;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &g_aoCompSetLayout;
        vkAllocateDescriptorSets(g_device, &dai, &g_aoCompSet);
    }
    VkDescriptorImageInfo dii{};
    dii.sampler = g_aoDepthSampler;
    dii.imageView = g_aoDepthView;
    dii.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wr{};
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = g_aoCompSet;
    wr.dstBinding = 0;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.descriptorCount = 1;
    wr.pImageInfo = &dii;
    vkUpdateDescriptorSets(g_device, 1, &wr, 0, nullptr);

    g_aoW = w;
    g_aoH = h;
    fprintf(stderr, "[SoH3D_VK] AO depth %ux%u ready\n", w, h);
    return true;
}

// (Re)create the square shadow depth map + framebuffer (on g_aoDepthRP) at the given resolution.
bool ensureShadowMap(uint32_t dim) {
    if (dim == 0)
        return false;
    if (g_shadowImg != VK_NULL_HANDLE && g_shadowDim == dim)
        return true;
    if (g_shadowImg != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(g_device);
        vkDestroyFramebuffer(g_device, g_shadowFb, nullptr);
        vkDestroyImageView(g_device, g_shadowView, nullptr);
        vkDestroyImage(g_device, g_shadowImg, nullptr);
        vkFreeMemory(g_device, g_shadowMem, nullptr);
        g_shadowFb = VK_NULL_HANDLE;
        g_shadowView = VK_NULL_HANDLE;
        g_shadowImg = VK_NULL_HANDLE;
        g_shadowMem = VK_NULL_HANDLE;
    }
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = kAoDepthFormat;
    ii.extent = { dim, dim, 1 };
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(g_device, &ii, nullptr, &g_shadowImg) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_device, g_shadowImg, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(g_device, &ai, nullptr, &g_shadowMem);
    vkBindImageMemory(g_device, g_shadowImg, g_shadowMem, 0);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = g_shadowImg;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = kAoDepthFormat;
    vi.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    vkCreateImageView(g_device, &vi, nullptr, &g_shadowView);

    VkFramebufferCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = g_aoDepthRP;
    fci.attachmentCount = 1;
    fci.pAttachments = &g_shadowView;
    fci.width = dim;
    fci.height = dim;
    fci.layers = 1;
    vkCreateFramebuffer(g_device, &fci, nullptr, &g_shadowFb);
    g_shadowDim = dim;
    fprintf(stderr, "[SoH3D_VK] shadow map %ux%u ready\n", dim, dim);
    return true;
}

// Record one model's depth-only groups into the currently-open depth render pass (AO pre-pass or
// shadow map). mp16 is the model->clip for that pass (camera clip for AO, light clip for shadow).
// Assumes the caller has begun the render pass and set viewport/scissor.
void recordDepthDraw(int modelId, const float* mp16, const float* mv16, int invertY, float aspectAdj,
                     const float* boneData, int boneCnt, unsigned long long midMask) {
    VkModel* m = ensureUploaded(modelId);
    if (!m)
        return;
    Ring& ring = g_rings[g_ctx.frameIndex];
    VkCommandBuffer cmd = g_ctx.cmd;

    VkUbo base{};
    memcpy(base.uMP, mp16, sizeof(base.uMP));
    base.uMP[0] *= aspectAdj;
    base.uMP[4] *= aspectAdj;
    base.uMP[8] *= aspectAdj;
    base.uMP[12] *= aspectAdj;
    memcpy(base.uMV, mv16 ? mv16 : mp16, sizeof(base.uMV));
    for (int k = 0; k < 64; k++)
        for (int e = 0; e < 16; e++)
            base.uBones[k * 16 + e] = (e % 5 == 0) ? 1.0f : 0.0f;
    if (boneData && boneCnt > 0) {
        int nb = boneCnt < 64 ? boneCnt : 64;
        for (int k = 0; k < nb; k++) {
            const float* s = boneData + k * 16;
            float* d = base.uBones + k * 16;
            for (int r = 0; r < 4; r++)
                for (int col = 0; col < 4; col++)
                    d[col * 4 + r] = s[r * 4 + col];
        }
    }
    base.uParams[0] = invertY ? -1.0f : 1.0f;
    base.uTintSkin[3] = (boneData && boneCnt > 0) ? 1.0f : 0.0f;

    int frontCW = (invertY != 0) ^ (gSoH3dFaceCullFlip != 0);
    bool vboBound = false;
    // depth/AO pass draws the whole model (the `hlroom` tint only affects the color pass).
    for (const VkGroup& grp : m->groups) {
        if (grp.cull)
            continue;
        if (grp.meshId >= 0 && grp.meshId < 64 && !((midMask >> grp.meshId) & 1ull))
            continue;
        if (ring.offset + g_uboStride > ring.capacity)
            return;

        VkUbo ubo = base;
        ubo.uParams[2] = grp.alphaTest ? grp.alphaRef : 0.0f; // alpha-test cutout depth
        const VkDeviceSize uboOff = ring.offset;
        memcpy((uint8_t*)ring.mapped + uboOff, &ubo, sizeof(ubo));
        ring.offset += g_uboStride;

        VkDescriptorSetAllocateInfo dai{};
        dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool = ring.pool;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &g_setLayout;
        VkDescriptorSet set;
        if (vkAllocateDescriptorSets(g_device, &dai, &set) != VK_SUCCESS)
            return;
        VkDescriptorBufferInfo bi{};
        bi.buffer = ring.ubo;
        bi.offset = uboOff;
        bi.range = sizeof(VkUbo);
        VkImageView view = g_dummyTex.view;
        VkSampler samp = g_dummySampler;
        if (grp.texIndex >= 0 && grp.texIndex < (int)m->textures.size()) {
            view = m->textures[grp.texIndex].view;
            samp = getSampler(grp.wrapS, grp.wrapT);
        }
        VkDescriptorImageInfo ii{};
        ii.sampler = samp;
        ii.imageView = view;
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w[2]{}; // binding 2 (shadow) is unused by the depth shader -> not written
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = set;
        w[0].dstBinding = 0;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[0].descriptorCount = 1;
        w[0].pBufferInfo = &bi;
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet = set;
        w[1].dstBinding = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1].descriptorCount = 1;
        w[1].pImageInfo = &ii;
        vkUpdateDescriptorSets(g_device, 2, w, 0, nullptr);

        bool doCull = grp.faceCull && vkFaceCullOn();
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getAoDepthPipeline(doCull, frontCW));
        if (!vboBound) {
            VkDeviceSize zero = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &m->vbo, &zero);
            vboBound = true;
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipeLayout, 0, 1, &set, 0, nullptr);
        vkCmdDraw(cmd, grp.count, 1, grp.first, 0);
    }
}

// Reset the per-frame UBO ring + descriptor pool once per RenderPass cycle (see g_frameStarted).
void startFrameOnce() {
    if (g_frameStarted)
        return;
    g_geomLast.swap(g_geomCur); // publish last frame's geometry capture; start a fresh one
    g_geomCur.clear();
    applyPendingEvict(); // drop any models flagged for reload before any draw records this frame
    Ring& r = g_rings[g_ctx.frameIndex];
    r.offset = 0;
    vkResetDescriptorPool(g_device, r.pool, 0);
    g_frameStarted = true;
}

} // namespace

extern "C" int SoH3D_Vk_Active(void) {
    return Fast::g_activeVulkanApi != nullptr ? 1 : 0;
}

extern "C" void SoH3D_Vk_SetProvider(SoH3DModelProvider fn) {
    g_provider = fn;
}

// geomscan bridge: copy the last completed frame's per-draw world AABBs out to the REPL (soh3d.c).
// Returns the count written; modelIds[i], mins[i*3..], maxs[i*3..] describe draw i.
extern "C" int SoH3D_GeomScanDump(int* modelIds, float* mins, float* maxs, int maxN) {
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

// Deferred model-cache eviction (mirror of the GL path). A request from another thread (the
// RmlUi stair-size row) names a model-id range; we drop those uploads at BeginPass — before this
// frame records any SoH3D draws — so the next draw re-uploads from the (already refreshed) CPU
// model. A full vkDeviceWaitIdle makes the destroy safe; it only happens on a config change.
static int g_evictLo = 0, g_evictHi = 0;
static bool g_evictPending = false;
extern "C" void SoH3D_Vk_RequestEvictRange(int lo, int hi) {
    g_evictLo = lo; g_evictHi = hi; g_evictPending = true;
}
static void applyPendingEvict() {
    if (!g_evictPending || g_device == VK_NULL_HANDLE)
        return;
    g_evictPending = false;
    vkDeviceWaitIdle(g_device);
    for (auto it = g_models.begin(); it != g_models.end();) {
        if (it->first >= g_evictLo && it->first < g_evictHi) {
            VkModel& m = it->second;
            for (auto& t : m.textures) {
                if (t.view) vkDestroyImageView(g_device, t.view, nullptr);
                if (t.image) vkDestroyImage(g_device, t.image, nullptr);
                if (t.mem) vkFreeMemory(g_device, t.mem, nullptr);
            }
            if (m.vbo) vkDestroyBuffer(g_device, m.vbo, nullptr);
            if (m.vboMem) vkFreeMemory(g_device, m.vboMem, nullptr);
            it = g_models.erase(it);
        } else {
            ++it;
        }
    }
}

extern "C" void SoH3D_Vk_BeginPass(void) {
    g_ctxValid = false;
    if (!Fast::g_activeVulkanApi)
        return;
    if (!Fast::g_activeVulkanApi->BeginSoH3DPass(g_ctx))
        return;
    if (!ensureResources(g_ctx))
        return;
    g_ctxValid = true;
    // Reset this frame-in-flight's UBO ring + descriptor pool exactly once per cycle (the AO depth
    // pre-pass may have begun the frame already; startFrameOnce guards against a double reset that
    // would wipe the prepass's UBO writes). The backend's in-flight fence (waited at StartFrame)
    // guarantees the previous use of this index has completed.
    startFrameOnce();
}

extern "C" void SoH3D_Vk_DrawModel(int modelId, const float* mp16, const float* mv16, int lit, int invertY,
                                   unsigned char r8, unsigned char g8, unsigned char b8, unsigned char a8,
                                   float aspectAdj, const float* boneData, int boneCnt,
                                   unsigned long long midMask, int sky, float uvOffU, float uvOffV,
                                   const void* matTex) {
    const std::unordered_map<int, int>* matTexMap = static_cast<const std::unordered_map<int, int>*>(matTex);
    if (!g_ctxValid)
        return;
    VkModel* m = ensureUploaded(modelId);
    if (!m)
        return;

    // Geometry-value capture (geomscan): world AABB = local AABB transformed by mv16 (model->world,
    // column-major to match the shader's ubo.uMV * pos). One record per draw; the sweep reads these
    // to flag misrendered geometry by VALUE (huge/degenerate world extent) with no screenshot/diff.
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

    Ring& ring = g_rings[g_ctx.frameIndex];
    VkCommandBuffer cmd = g_ctx.cmd;

    // Base UBO fields shared by all groups of this draw (per-group alphaRef/depthOffset patched below).
    VkUbo base{};
    memcpy(base.uMP, mp16, sizeof(base.uMP));
    base.uMP[0] *= aspectAdj; // mirror Fast3D's AdjXForAspectRatio (MP column 0, row-major 0/4/8/12)
    base.uMP[4] *= aspectAdj;
    base.uMP[8] *= aspectAdj;
    base.uMP[12] *= aspectAdj;
    memcpy(base.uMV, mv16, sizeof(base.uMV));
    for (int k = 0; k < 64; k++)
        for (int e = 0; e < 16; e++)
            base.uBones[k * 16 + e] = (e % 5 == 0) ? 1.0f : 0.0f; // identity
    if (boneData && boneCnt > 0) {
        // boneData is row-major (M*v). GL uploads it with glUniformMatrix4fv(..., GL_TRUE, ...) which
        // transposes on upload, so GLSL stores M itself. Vulkan std140 mat4 is column-major with no
        // transpose-on-upload, and a raw memcpy of row-major data stores M^T -> wrong skinning. So
        // transpose each bone matrix CPU-side to match GL exactly.
        int nb = boneCnt < 64 ? boneCnt : 64;
        for (int k = 0; k < nb; k++) {
            const float* s = boneData + k * 16;
            float* d = base.uBones + k * 16;
            for (int r = 0; r < 4; r++)
                for (int col = 0; col < 4; col++)
                    d[col * 4 + r] = s[r * 4 + col];
        }
    }
    base.uParams[0] = invertY ? -1.0f : 1.0f;
    // Gate the half-Lambert form term on the scene-lighting master toggle (#72), matching the GL
    // pass (soh3d_gl.cpp drawOne): lit && gSoH3dLightEnable. -1 (uninitialised, Vulkan-only run) reads
    // as ON; the REPL/menu set it to 0/1 explicitly.
    base.uParams[1] = (lit && gSoH3dLightEnable != 0) ? 1.0f : 0.0f;
    base.uTintSkin[0] = r8 / 255.0f;
    base.uTintSkin[1] = g8 / 255.0f;
    base.uTintSkin[2] = b8 / 255.0f;
    base.uTintSkin[3] = (boneData && boneCnt > 0) ? 1.0f : 0.0f;

    // World-space sun direction (set per frame by soh3d.c into the GL pass's global).
    base.uLightDir[0] = gSoH3dLightDirWorld[0];
    base.uLightDir[1] = gSoH3dLightDirWorld[1];
    base.uLightDir[2] = gSoH3dLightDirWorld[2];
    base.uLightDir[3] = sky ? 1.0f : 0.0f; // skybox dome: pin to far plane in the vertex shader
    base.uExtra[0] = a8 / 255.0f;          // per-draw opacity (dawn/dusk dome cross-fade); 1 = opaque
    base.uExtra[1] = uvOffU;               // texcoord scroll U (cloud-band drift, #28b); 0 = none
    base.uExtra[2] = uvOffV;               // texcoord scroll V
    // Dynamic sun-shadow: world->light-clip matrix + tunables (set per RenderPass by SetShadow). The
    // shadow term darkens BOTH lit and unlit draws so characters cast onto the OoT3D ground.
    if (g_shadowOn)
        memcpy(base.uLightVP, g_shadowLightVP, sizeof(base.uLightVP));
    base.uShadow[0] = g_shadowOn ? 1.0f : 0.0f;
    base.uShadow[1] = gSoH3dShadowBias;
    base.uShadow[2] = gSoH3dShadowStrength;
    base.uShadow[3] = g_shadowDim ? 1.0f / (float)g_shadowDim : 0.0f;
    // N64/OoT3D F3DEX fog, from envCtx.lightSettings + gSPFogPosition via soh3d.c. uFog2 = the F3DEX
    // (mul, offset) the world shader applies to the NDC z, exactly like the RSP fog stage.
    base.uFog[0] = gSoH3dFogColor[0];
    base.uFog[1] = gSoH3dFogColor[1];
    base.uFog[2] = gSoH3dFogColor[2];
    base.uFog[3] = gSoH3dFogEnable ? 1.0f : 0.0f;
    base.uFog2[0] = gSoH3dFogMul;
    base.uFog2[1] = gSoH3dFogOffset;
    base.uFog2[2] = 0.0f;
    base.uFog2[3] = 0.0f;
    // #110 additive env-ambient floor colour (live, time-blended from envCtx.lightSettings.ambient
    // via soh3d.c). The per-group coef (uAmbient[3]) is set below, scoped to vertex-lit scene geom.
    extern float gSoH3dWorldAmbColor[3];
    base.uAmbient[0] = gSoH3dWorldAmbColor[0];
    base.uAmbient[1] = gSoH3dWorldAmbColor[1];
    base.uAmbient[2] = gSoH3dWorldAmbColor[2];
    base.uAmbient[3] = 0.0f;
    bool forceBlend = (a8 < 255);          // translucent draw -> alpha-over even if the material is opaque

    bool vboBound = false;
    // #29 diagnostic: REPL `hlroom <n>` tints room-mesh group N red (room mesh = >20 groups) so a
    // suspect backdrop group (e.g. the untextured "dome") can be identified by index live.
    extern int gSoH3dHlGroup;
    bool roomHl = (gSoH3dHlGroup >= 0 && m->groups.size() > 20);
    int gIdx = -1;
    for (const VkGroup& grp : m->groups) {
        gIdx++;
        if (grp.cull)
            continue;
        if (grp.meshId >= 0 && grp.meshId < 64 && !((midMask >> grp.meshId) & 1ull))
            continue;
        if (ring.offset + g_uboStride > ring.capacity)
            return; // ring exhausted this frame

        VkUbo ubo = base;
        // #29: tint the highlighted room group bright red (keeps the full scene rendering) to ID it.
        if (roomHl && gIdx == gSoH3dHlGroup) { ubo.uTintSkin[0] = 1.0f; ubo.uTintSkin[1] = 0.0f; ubo.uTintSkin[2] = 0.0f; }
        ubo.uParams[2] = grp.alphaTest ? grp.alphaRef : 0.0f;
        ubo.uParams[3] = grp.polygonOffset;
        // OoT3D world (scene) combiner port (docs/oot3d_world_lighting_re.md): apply the
        // per-material stage-0 TEV RGB scale (Kokiri grass = x2) to vertex-lit scene geometry.
        // uExtra.w stays 1.0 for everything else, so the shader's saturate(...)*scale is a no-op
        // for the unchanged paths. Gated by REPL `worldlit` for A/B against the oracle.
        extern int gSoH3dWorldLit;
        ubo.uExtra[3] = (grp.vertexLighting && gSoH3dWorldLit) ? grp.combScaleRGB : 1.0f;
        // #110: additive env-ambient floor coef, scoped exactly like combScale (vertex-lit scene
        // geom only; 0 elsewhere -> shader no-op). gSoH3dWorldAmb is the live-derived coefficient.
        // MODULATED per-material by matAmbient (OoT3D's `ambient * matAmbient`, render.ts:651): the
        // Kokiri grass has matAmbient=WHITE so it takes the full blue floor, while the warm dirt path
        // has a lower/warmer matAmbient so it keeps its warm tan instead of washing out to grey (#110
        // ground regression). The scene-constant colour (base.uAmbient.xyz) is scaled by matAmbient.
        // Per-material modulation by matAmbient (OoT3D's `ambient * matAmbient`, render.ts:651): a
        // material with a lower/warmer matAmbient takes less of the blue floor. (In Kokiri the grass
        // AND the dirt path share dif=0/amb=white, so the floor can't separate them by material — the
        // grass-vs-path blue difference lives in the baked vertex colour/texture and only the full
        // vertex-lighting port (#111) reproduces it. The coef is kept low (0.02) so a uniform floor
        // is a subtle cool ambient that does NOT wash the warm path out to grey — see #110 notes.)
        extern float gSoH3dWorldAmb;
        bool ambGroup = (grp.vertexLighting && gSoH3dWorldLit);
        ubo.uAmbient[0] = base.uAmbient[0] * grp.matAmbient[0];
        ubo.uAmbient[1] = base.uAmbient[1] * grp.matAmbient[1];
        ubo.uAmbient[2] = base.uAmbient[2] * grp.matAmbient[2];
        ubo.uAmbient[3] = ambGroup ? gSoH3dWorldAmb : 0.0f;
        const VkDeviceSize uboOff = ring.offset;
        memcpy((uint8_t*)ring.mapped + uboOff, &ubo, sizeof(ubo));
        ring.offset += g_uboStride;

        VkDescriptorSetAllocateInfo dai{};
        dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool = ring.pool;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &g_setLayout;
        VkDescriptorSet set;
        if (vkAllocateDescriptorSets(g_device, &dai, &set) != VK_SUCCESS)
            return;

        VkDescriptorBufferInfo bi{};
        bi.buffer = ring.ubo;
        bi.offset = uboOff;
        bi.range = sizeof(VkUbo);

        // Facial material-anim: a per-material override (eye/mouth frame) wins over the static tex.
        int texIndex = grp.texIndex;
        if (matTexMap && grp.materialIndex >= 0) {
            auto ov = matTexMap->find(grp.materialIndex);
            if (ov != matTexMap->end() && ov->second >= 0) texIndex = ov->second;
        }
        VkImageView view = g_dummyTex.view;
        VkSampler samp = g_dummySampler;
        if (texIndex >= 0 && texIndex < (int)m->textures.size()) {
            view = m->textures[texIndex].view;
            samp = getSampler(grp.wrapS, grp.wrapT);
        }
        VkDescriptorImageInfo ii{};
        ii.sampler = samp;
        ii.imageView = view;
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Shadow map (binding 2): the live depth render when shadows are on, else the dummy depth.
        VkDescriptorImageInfo si{};
        si.sampler = g_aoDepthSampler;
        si.imageView = (g_shadowOn && g_shadowView != VK_NULL_HANDLE) ? g_shadowView : g_dummyDepth.view;
        si.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet w[3]{};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = set;
        w[0].dstBinding = 0;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[0].descriptorCount = 1;
        w[0].pBufferInfo = &bi;
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet = set;
        w[1].dstBinding = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1].descriptorCount = 1;
        w[1].pImageInfo = &ii;
        w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[2].dstSet = set;
        w[2].dstBinding = 2;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[2].descriptorCount = 1;
        w[2].pImageInfo = &si;
        vkUpdateDescriptorSets(g_device, 3, w, 0, nullptr);

        // Translucent draw over an opaque material: synthesize a standard alpha-over pipeline (the
        // VkGroup blend-factor defaults are SRC_ALPHA / ONE_MINUS_SRC_ALPHA) so uExtra.x composites,
        // mirroring the GL path's forceBlend. Depth-write/offset/etc. are inherited from the group.
        VkGroup gb = grp;
        if (forceBlend && !grp.blendEnable) {
            gb.blendEnable = 1;
            gb.bSrcRGB = 0x0302; gb.bDstRGB = 0x0303; gb.bEqRGB = 0x8006; // SRC_ALPHA / 1-SRC_ALPHA / ADD
            gb.bSrcA = 0x0302;   gb.bDstA = 0x0303;   gb.bEqA = 0x8006;
        }
        // Front-face winding flips with invertY (clip.y negated in the vertex shader); the flip
        // toggle lets the correct convention be found live. See the GL backend's drawOne.
        int frontCW = (invertY != 0) ^ (gSoH3dFaceCullFlip != 0);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getPipeline(gb, frontCW));
        vkCmdSetViewport(cmd, 0, 1, &g_ctx.viewport);
        vkCmdSetScissor(cmd, 0, 1, &g_ctx.scissor);
        vkCmdSetBlendConstants(cmd, grp.blendColor);
        if (!vboBound) {
            VkDeviceSize zero = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &m->vbo, &zero);
            vboBound = true;
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipeLayout, 0, 1, &set, 0, nullptr);
        vkCmdDraw(cmd, grp.count, 1, grp.first, 0);
    }
}

extern "C" void SoH3D_Vk_EndPass(void) {
    g_ctxValid = false;
    g_frameStarted = false; // next RenderPass cycle resets the ring afresh
    g_aoPrepassActive = false;
    g_shadowOn = false; // each cycle re-establishes the shadow term via SetShadow
}

// --- AO offscreen pre-pass + composite -----------------------------------------------------------

// Begin the private depth render pass (clear to far). Returns 0 if AO is off / unavailable, in which
// case the caller skips the prepass draws + composite. Ends the FB pass (offscreen pass can't nest).
extern "C" int SoH3D_Vk_BeginDepthPrepass(void) {
    g_aoPrepassActive = false;
    if (!gSoH3dAoEnable || !Fast::g_activeVulkanApi)
        return 0;
    if (!Fast::g_activeVulkanApi->BeginSoH3DOffscreen(g_ctx))
        return 0;
    if (!ensureResources(g_ctx) || !ensureAoResources())
        return 0;
    startFrameOnce(); // the prepass records the first draws this cycle -> own the ring reset
    // Size the private depth image to cover the viewport extent (incl. any offset), so the
    // composite's gl_FragCoord indexes it directly.
    uint32_t w = (uint32_t)(g_ctx.viewport.x + g_ctx.viewport.width);
    uint32_t h = (uint32_t)(g_ctx.viewport.y + g_ctx.viewport.height);
    if (!ensureAoDepth(w, h))
        return 0;

    VkClearValue clr{};
    clr.depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = g_aoDepthRP;
    rp.framebuffer = g_aoDepthFb;
    rp.renderArea.offset = { 0, 0 };
    rp.renderArea.extent = { g_aoW, g_aoH };
    rp.clearValueCount = 1;
    rp.pClearValues = &clr;
    vkCmdBeginRenderPass(g_ctx.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(g_ctx.cmd, 0, 1, &g_ctx.viewport);
    vkCmdSetScissor(g_ctx.cmd, 0, 1, &g_ctx.scissor);
    g_aoPrepassActive = true;
    return 1;
}

extern "C" void SoH3D_Vk_DepthPrepassDraw(int modelId, const float* mp16, const float* mv16, int invertY,
                                          float aspectAdj, const float* boneData, int boneCnt,
                                          unsigned long long midMask, int sky) {
    if (!g_aoPrepassActive || sky) // sky goes to the far plane -> reads 1.0 -> no AO; just skip it
        return;
    recordDepthDraw(modelId, mp16, mv16, invertY, aspectAdj, boneData, boneCnt, midMask);
}

extern "C" void SoH3D_Vk_EndDepthPrepass(void) {
    if (!g_aoPrepassActive)
        return;
    vkCmdEndRenderPass(g_ctx.cmd); // depth image now in DEPTH_STENCIL_READ_ONLY_OPTIMAL
    g_aoPrepassActive = false;
}

// Full-screen SSAO multiply onto the scene colour, recorded INSIDE the main FB pass (after the
// visible model draws). Sampled from the private depth written by the prepass.
extern "C" void SoH3D_Vk_AoComposite(void) {
    if (!g_ctxValid || !gSoH3dAoEnable || !g_aoResReady || g_aoCompSet == VK_NULL_HANDLE || g_aoW == 0)
        return;
    VkCommandBuffer cmd = g_ctx.cmd;
    AoPush pc{};
    pc.texel[0] = 1.0f / (float)g_aoW;
    pc.texel[1] = 1.0f / (float)g_aoH;
    pc.radius = gSoH3dAoRadius;
    pc.strength = gSoH3dAoStrength;
    pc.bias = gSoH3dAoBias;
    pc.maxDiff = gSoH3dAoMaxDiff;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_aoCompPipe);
    vkCmdSetViewport(cmd, 0, 1, &g_ctx.viewport);
    vkCmdSetScissor(cmd, 0, 1, &g_ctx.scissor);
    vkCmdPushConstants(cmd, g_aoCompPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_aoCompPipeLayout, 0, 1, &g_aoCompSet, 0,
                            nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

// --- Dynamic sun-shadow offscreen pass -----------------------------------------------------------

// Begin the shadow depth map render (light's POV). Returns 0 if shadows are off / no focus yet /
// resources unavailable. Ends the FB pass (offscreen pass can't nest), like the AO pre-pass.
extern "C" int SoH3D_Vk_BeginShadowPass(void) {
    g_shadowPassActive = false;
    if (!gSoH3dShadowEnable || !gSoH3dShadowHasFocus || !Fast::g_activeVulkanApi)
        return 0;
    if (!Fast::g_activeVulkanApi->BeginSoH3DOffscreen(g_ctx))
        return 0;
    if (!ensureResources(g_ctx) || !ensureAoResources()) // ensureAoResources owns g_aoDepthRP + depth pipes
        return 0;
    startFrameOnce();
    if (!ensureShadowMap(kShadowRes))
        return 0;

    VkClearValue clr{};
    clr.depthStencil = { 1.0f, 0 }; // empty texels read far -> ground not under a caster is lit
    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = g_aoDepthRP;
    rp.framebuffer = g_shadowFb;
    rp.renderArea.offset = { 0, 0 };
    rp.renderArea.extent = { g_shadowDim, g_shadowDim };
    rp.clearValueCount = 1;
    rp.pClearValues = &clr;
    vkCmdBeginRenderPass(g_ctx.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = (float)g_shadowDim;
    vp.height = (float)g_shadowDim;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    VkRect2D sc{};
    sc.offset = { 0, 0 };
    sc.extent = { g_shadowDim, g_shadowDim };
    vkCmdSetViewport(g_ctx.cmd, 0, 1, &vp);
    vkCmdSetScissor(g_ctx.cmd, 0, 1, &sc);
    g_shadowPassActive = true;
    return 1;
}

// Record one shadow caster into the shadow map. mp16 = lightVP * (model->world); invertY forced 0
// (the fragment shadow projection samples uLightVP*world directly, matching the GL path).
extern "C" void SoH3D_Vk_ShadowCasterDraw(int modelId, const float* mp16, const float* mv16,
                                          const float* boneData, int boneCnt, unsigned long long midMask) {
    if (!g_shadowPassActive)
        return;
    recordDepthDraw(modelId, mp16, mv16, /*invertY=*/0, /*aspectAdj=*/1.0f, boneData, boneCnt, midMask);
}

// #72: record the N64 opaque world-space caster triangle soup into the open shadow render pass, so
// N64-drawn geometry (N64 Link, unreplaced actors, scene mesh) casts a sun shadow too — mirroring
// the GL SoH3D_GL_ShadowCasterTris. worldXYZ = triCount*3 verts of xyz; positions are already
// WORLD-space, so uMP = lightVP and uMV = identity, no skinning, no cull (depth-only). The vertices
// are staged into a per-frame host-visible buffer kept alive until this frame's GPU work completes.
extern "C" void SoH3D_Vk_ShadowCasterTris(const float* worldXYZ, size_t triCount, const float* lightVP16) {
    if (!g_shadowPassActive || !worldXYZ || triCount == 0)
        return;
    const size_t vtxCount = triCount * 3;
    const VkDeviceSize bytes = (VkDeviceSize)vtxCount * sizeof(SoH3DGlVtx);

    if (g_n64CasterBufs.size() < g_rings.size())
        g_n64CasterBufs.resize(g_rings.size());
    N64CasterBuf& cb = g_n64CasterBufs[g_ctx.frameIndex];
    if (cb.capacity < bytes) {
        // Grow (and free any previous undersized buffer). Safe here: this frame index's prior GPU
        // work has completed (the caller's frame fence gated re-recording).
        if (cb.buf) {
            vkDestroyBuffer(g_device, cb.buf, nullptr);
            vkFreeMemory(g_device, cb.mem, nullptr);
            cb = N64CasterBuf{};
        }
        VkDeviceSize cap = bytes + bytes / 2 + 4096; // headroom to amortise regrowth
        makeBuffer(cap, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, cb.buf, cb.mem,
                   &cb.mapped);
        cb.capacity = cap;
    }
    // Fill SoH3DGlVtx with pos only (other fields unused: uSkin/uLit off; depth-only frag).
    SoH3DGlVtx* dst = (SoH3DGlVtx*)cb.mapped;
    for (size_t i = 0; i < vtxCount; i++) {
        SoH3DGlVtx v{};
        v.pos[0] = worldXYZ[i * 3 + 0];
        v.pos[1] = worldXYZ[i * 3 + 1];
        v.pos[2] = worldXYZ[i * 3 + 2];
        dst[i] = v;
    }

    Ring& ring = g_rings[g_ctx.frameIndex];
    VkCommandBuffer cmd = g_ctx.cmd;
    if (ring.offset + g_uboStride > ring.capacity)
        return;

    VkUbo ubo{};
    memcpy(ubo.uMP, lightVP16, sizeof(ubo.uMP)); // positions are world-space -> clip = lightVP*world
    float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    memcpy(ubo.uMV, identity, sizeof(ubo.uMV));
    for (int k = 0; k < 64; k++)
        for (int e = 0; e < 16; e++)
            ubo.uBones[k * 16 + e] = (e % 5 == 0) ? 1.0f : 0.0f;
    ubo.uParams[0] = 1.0f;       // invertY off
    ubo.uTintSkin[3] = 0.0f;     // no skinning -> aPos used directly
    const VkDeviceSize uboOff = ring.offset;
    memcpy((uint8_t*)ring.mapped + uboOff, &ubo, sizeof(ubo));
    ring.offset += g_uboStride;

    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = ring.pool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &g_setLayout;
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(g_device, &dai, &set) != VK_SUCCESS)
        return;
    VkDescriptorBufferInfo bi{};
    bi.buffer = ring.ubo;
    bi.offset = uboOff;
    bi.range = sizeof(VkUbo);
    VkDescriptorImageInfo ii{};
    ii.sampler = g_dummySampler;
    ii.imageView = g_dummyTex.view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w[2]{};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = set;
    w[0].dstBinding = 0;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].descriptorCount = 1;
    w[0].pBufferInfo = &bi;
    w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[1].dstSet = set;
    w[1].dstBinding = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[1].descriptorCount = 1;
    w[1].pImageInfo = &ii;
    vkUpdateDescriptorSets(g_device, 2, w, 0, nullptr);

    // No-cull depth pipeline: N64 winding varies, cast from both sides (depth-only).
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getAoDepthPipeline(/*doCull=*/false, /*frontCW=*/0));
    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &cb.buf, &zero);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipeLayout, 0, 1, &set, 0, nullptr);
    vkCmdDraw(cmd, (uint32_t)vtxCount, 1, 0, 0);
}

extern "C" void SoH3D_Vk_EndShadowPass(void) {
    if (!g_shadowPassActive)
        return;
    vkCmdEndRenderPass(g_ctx.cmd); // shadow map now in DEPTH_STENCIL_READ_ONLY_OPTIMAL
    g_shadowPassActive = false;
}

// Enable/disable the shadow term for the upcoming visible model draws (set by the dispatcher after
// the shadow map renders). lightVP16 = world->light-clip (computeLightVP); ignored when on == 0.
extern "C" void SoH3D_Vk_SetShadow(int on, const float* lightVP16) {
    g_shadowOn = (on != 0);
    if (g_shadowOn && lightVP16)
        memcpy(g_shadowLightVP, lightVP16, sizeof(g_shadowLightVP));
}

#endif // ENABLE_VULKAN
