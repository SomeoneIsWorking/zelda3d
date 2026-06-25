// SoH3D SDL3 GPU render path. See include/fast/soh3d_sdl3gpu.h for the role split with soh3d_gl.cpp.
//
// Renders the collected OoT3D draw items (textured / GPU-skinned / half-Lambert-lit, per-group
// blend + depth-write + alpha-test + decal depth-bias + mesh_id visibility) as OPS appended into
// the SDL3 GPU backend's deferred op-list, so the 3DS content replays in the SAME single render
// pass as the N64 geometry (depth-correct interleave, NO separate-pass handshake). This is the
// unified-renderer model (user directive 2026-06-26). The model shaders/UBO are the Vulkan path's
// (soh3d_vk.cpp) verbatim, retargeted to SDL3 GPU's SPIR-V resource-set binding model.
#ifdef ENABLE_SDL3GPU

#include "fast/soh3d_sdl3gpu.h"
#include "fast/backends/gfx_sdl3gpu.h"

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

using Fast::GfxRenderingAPISdl3Gpu;
using Fast::g_activeSdl3GpuApi;

// ---- Shared scene/light/effect globals (owned by soh3d_gl.cpp, set per frame by soh3d.c) ----
extern "C" float gSoH3dLightDirWorld[3];
extern "C" int gSoH3dLightEnable;
extern "C" int gSoH3dFaceCull;
extern "C" int gSoH3dFaceCullFlip;
extern "C" int gSoH3dFogEnable;
extern "C" float gSoH3dFogColor[3];
extern "C" float gSoH3dFogMul;
extern "C" float gSoH3dFogOffset;
extern "C" float gSoH3dWorldAmbColor[3];
extern "C" float gSoH3dWorldAmb;
extern "C" int gSoH3dWorldLit;
extern "C" int gSoH3dHlGroup;

static int sgFaceCullOn() {
    if (gSoH3dFaceCull < 0) {
        const char* e = getenv("SOH3D_FACECULL");
        gSoH3dFaceCull = (e && e[0] == '0') ? 0 : 1; // default ON
    }
    return gSoH3dFaceCull;
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
        fprintf(stderr, "[SoH3D_SG] shader parse failed: %s\n", shader.getInfoLog());
        return false;
    }
    glslang::TProgram prog;
    prog.addShader(&shader);
    if (!prog.link(msg)) {
        fprintf(stderr, "[SoH3D_SG] shader link failed: %s\n", prog.getInfoLog());
        return false;
    }
    glslang::SpvOptions opt;
    opt.disableOptimizer = true;
    glslang::GlslangToSpv(*prog.getIntermediate(stage), spv, &opt);
    return !spv.empty();
}

// The model UBO, declared once and shared by both stages. SDL3 GPU SPIR-V requires vertex uniform
// buffers in descriptor set 1 and fragment uniform buffers in set 3, with fragment samplers in set
// 2. The body below is byte-identical to soh3d_vk.cpp's kVert/kFrag; only the set= decorations
// differ (Vulkan put everything in set 0).
#define SG_UBO_BODY \
    "    mat4 uMP;\n" \
    "    mat4 uMV;\n" \
    "    mat4 uBones[32];\n" \
    "    vec4 uLightDir;\n" \
    "    vec4 uParams;\n" \
    "    vec4 uTintSkin;\n" \
    "    vec4 uExtra;\n" \
    "    mat4 uLightVP;\n" \
    "    vec4 uShadow;\n" \
    "    vec4 uFog;\n" \
    "    vec4 uFog2;\n" \
    "    vec4 uAmbient;\n"

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
    "layout(set=1, binding=0, std140) uniform UBO {\n" SG_UBO_BODY "} ubo;\n"
    "void main() {\n"
    "    vColor = aColor;\n"
    "    vec3 sp, nM;\n"
    "    if (ubo.uTintSkin.w > 0.5) {\n"
    "        vec4 acc = vec4(0.0); nM = vec3(0.0);\n"
    "        for (int i = 0; i < 4; i++) {\n"
    "            acc += aBoneW[i] * (ubo.uBones[int(aBoneId[i])] * vec4(aPos, 1.0));\n"
    "            nM  += aBoneW[i] * (mat3(ubo.uBones[int(aBoneId[i])]) * aNrm);\n"
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
    "layout(set=3, binding=0, std140) uniform UBO {\n" SG_UBO_BODY "} ubo;\n"
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
    "    vec3 rgb = t.rgb * vColor.rgb * shade;\n"
    "    if (ubo.uParams.y < 0.5)\n"
    "        rgb = clamp(rgb, 0.0, 1.0) * ubo.uExtra.w;\n"
    "    if (ubo.uAmbient.w > 0.0)\n"
    "        rgb = clamp(rgb + ubo.uAmbient.xyz * ubo.uAmbient.w, 0.0, 1.0);\n"
    "    if (ubo.uFog.w > 0.5 && ubo.uLightDir.w < 0.5) {\n"
    "        float f = clamp(vFogDist * ubo.uFog2.x + ubo.uFog2.y, 0.0, 255.0) * (1.0 / 255.0);\n"
    "        rgb = mix(rgb, ubo.uFog.xyz, f);\n"
    "    }\n"
    "    frag = vec4(rgb, t.a * vColor.a * ubo.uExtra.x);\n"
    "}\n";

// std140 UBO layout matching the shader block (identical to soh3d_vk.cpp VkUbo).
struct SgUbo {
    float uMP[16];
    float uMV[16];
    float uBones[32 * 16];
    float uLightDir[4];
    float uParams[4];
    float uTintSkin[4];
    float uExtra[4];
    float uLightVP[16];
    float uShadow[4];
    float uFog[4];
    float uFog2[4];
    float uAmbient[4];
};

struct SgGroup {
    uint32_t first = 0, count = 0;
    int texIndex = -1;
    int alphaTest = 0;
    float alphaRef = 0.0f;
    unsigned wrapS = 0x2901, wrapT = 0x2901;
    int blendEnable = 0;
    unsigned bSrcRGB = 0x0302, bDstRGB = 0x0303, bEqRGB = 0x8006;
    unsigned bSrcA = 1, bDstA = 0, bEqA = 0x8006;
    int depthWrite = 1;
    float polygonOffset = 0.0f;
    int cull = 0;
    int faceCull = 0;
    int meshId = -1;
    int materialIndex = -1;
    int vertexLighting = 0;
    float combScaleRGB = 1.0f;
    float matAmbient[3] = { 1.0f, 1.0f, 1.0f };
    float matDiffuse[3] = { 1.0f, 1.0f, 1.0f };
};

struct SgModel {
    bool uploaded = false, failed = false;
    SDL_GPUBuffer* vbo = nullptr;
    std::vector<SgGroup> groups;
    std::vector<SDL_GPUTexture*> textures;
};

// ---- module state ----
SoH3DModelProvider g_provider = nullptr;
std::unordered_map<int, SgModel> g_models;

SDL_GPUDevice* g_device = nullptr;
bool g_resReady = false;
SDL_GPUShader* g_vert = nullptr;
SDL_GPUShader* g_frag = nullptr;
SDL_GPUTexture* g_dummyTex = nullptr; // 1x1 white (untextured groups + shadow slot when off)
std::map<uint32_t, SDL_GPUSampler*> g_samplers; // key (wrapS<<16)|wrapT
SDL_GPUSampler* g_dummySampler = nullptr;

// Pipeline cache: key = blend/depth/cull flags + 6 blend params + frontCW.
struct PipeKey {
    std::array<uint32_t, 8> v;
    bool operator<(const PipeKey& o) const {
        return v < o.v;
    }
};
std::map<PipeKey, SDL_GPUGraphicsPipeline*> g_pipelines;

bool g_ctxValid = false;

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

SDL_GPUSampler* getSampler(unsigned wrapS, unsigned wrapT) {
    uint32_t key = (wrapS << 16) | (wrapT & 0xFFFF);
    auto it = g_samplers.find(key);
    if (it != g_samplers.end())
        return it->second;
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = SDL_GPU_FILTER_LINEAR;
    si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = wrapMode(wrapS);
    si.address_mode_v = wrapMode(wrapT);
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    SDL_GPUSampler* s = SDL_CreateGPUSampler(g_device, &si);
    g_samplers[key] = s;
    return s;
}

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

// Upload an RGBA8 texture (one-shot copy pass on a private command buffer).
SDL_GPUTexture* uploadTexture(int w, int h, const unsigned char* rgba) {
    if (w <= 0 || h <= 0)
        w = h = 1;
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = (uint32_t)w;
    ci.height = (uint32_t)h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(g_device, &ci);

    const uint32_t size = (uint32_t)w * h * 4;
    static const unsigned char white[4] = { 255, 255, 255, 255 };
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = rgba ? size : 4;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(g_device, &tci);
    void* mapped = SDL_MapGPUTransferBuffer(g_device, tb, false);
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
    SDL_SubmitGPUCommandBuffer(c);
    SDL_ReleaseGPUTransferBuffer(g_device, tb);
    return tex;
}

bool ensureResources() {
    if (g_resReady)
        return true;
    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;
    if (!api)
        return false;
    g_device = api->GpuDevice();
    if (!g_device)
        return false;

    std::vector<uint32_t> vsSpv, fsSpv;
    if (!CompileGlsl(EShLangVertex, kVert, vsSpv) || !CompileGlsl(EShLangFragment, kFrag, fsSpv))
        return false;

    SDL_GPUShaderCreateInfo vci{};
    vci.code_size = vsSpv.size() * sizeof(uint32_t);
    vci.code = (const Uint8*)vsSpv.data();
    vci.entrypoint = "main";
    vci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vci.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vci.num_uniform_buffers = 1;
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
        fprintf(stderr, "[SoH3D_SG] shader create failed: %s\n", SDL_GetError());
        return false;
    }

    g_dummyTex = uploadTexture(1, 1, nullptr);
    g_dummySampler = getSampler(0x2901, 0x2901);
    g_resReady = true;
    fprintf(stderr, "[SoH3D_SG] resources ready (unified op model)\n");
    return true;
}

SDL_GPUGraphicsPipeline* getPipeline(const SgGroup& g, int frontCW) {
    bool doCull = g.faceCull && sgFaceCullOn();
    PipeKey key;
    key.v = { (uint32_t)((g.blendEnable ? 1u : 0u) | (g.depthWrite ? 2u : 0u) | (doCull ? 4u : 0u) |
                         (doCull && frontCW ? 8u : 0u)),
              g.bSrcRGB, g.bDstRGB, g.bEqRGB, g.bSrcA, g.bDstA, g.bEqA, 0u };
    auto it = g_pipelines.find(key);
    if (it != g_pipelines.end())
        return it->second;

    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;

    // Vertex input: SoH3DGlVtx (pos3, nrm3, uv2, boneId4, boneW4, color4).
    SDL_GPUVertexAttribute attrs[6]{};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (uint32_t)offsetof(SoH3DGlVtx, pos) };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (uint32_t)offsetof(SoH3DGlVtx, nrm) };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(SoH3DGlVtx, uv) };
    attrs[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (uint32_t)offsetof(SoH3DGlVtx, boneIds) };
    attrs[4] = { 4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (uint32_t)offsetof(SoH3DGlVtx, weights) };
    attrs[5] = { 5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (uint32_t)offsetof(SoH3DGlVtx, color) };
    SDL_GPUVertexBufferDescription vb{};
    vb.slot = 0;
    vb.pitch = sizeof(SoH3DGlVtx);
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
        fprintf(stderr, "[SoH3D_SG] pipeline create failed: %s\n", SDL_GetError());
    g_pipelines[key] = pipe;
    return pipe;
}

SgModel* ensureUploaded(int modelId) {
    SgModel& m = g_models[modelId];
    if (m.uploaded)
        return &m;
    if (m.failed)
        return nullptr;
    const SoH3DGlGroup* groups = nullptr;
    const SoH3DGlTex* texs = nullptr;
    int groupCount = 0, texCount = 0;
    if (!g_provider || !g_provider(modelId, &groups, &groupCount, &texs, &texCount) || groupCount <= 0) {
        fprintf(stderr, "[SoH3D_SG] model %d unavailable from provider\n", modelId);
        m.failed = true;
        return nullptr;
    }

    std::vector<SoH3DGlVtx> all;
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
        all.insert(all.end(), groups[i].verts, groups[i].verts + groups[i].vertCount);
        m.groups.push_back(g);
    }

    // Device vertex buffer via a transfer-buffer copy.
    const uint32_t vbBytes = (uint32_t)(all.size() * sizeof(SoH3DGlVtx));
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

    for (int i = 0; i < texCount; i++)
        m.textures.push_back(uploadTexture(texs[i].w, texs[i].h, texs[i].rgba));

    m.uploaded = true;
    fprintf(stderr, "[SoH3D_SG] uploaded model %d: %d groups, %d textures, %zu verts\n", modelId, groupCount,
            texCount, all.size());
    return &m;
}

// Deferred model eviction (mirror of the GL/VK path).
int g_evictLo = 0, g_evictHi = 0;
bool g_evictPending = false;
void applyPendingEvict() {
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

// One captured per-group draw, replayed inside the unified render pass.
struct DrawGroup {
    SDL_GPUGraphicsPipeline* pipeline;
    SDL_GPUTexture* tex;
    SDL_GPUSampler* samp;
    uint32_t first, count;
    std::array<uint8_t, sizeof(SgUbo)> ubo;
};

} // namespace

extern "C" int SoH3D_Sg_Active(void) {
    return Fast::g_activeSdl3GpuApi != nullptr ? 1 : 0;
}

extern "C" void SoH3D_Sg_SetProvider(SoH3DModelProvider fn) {
    g_provider = fn;
}

extern "C" void SoH3D_Sg_RequestEvictRange(int lo, int hi) {
    g_evictLo = lo;
    g_evictHi = hi;
    g_evictPending = true;
}

extern "C" void SoH3D_Sg_BeginPass(void) {
    g_ctxValid = false;
    if (!g_activeSdl3GpuApi)
        return;
    if (!ensureResources())
        return;
    applyPendingEvict();
    g_ctxValid = true;
}

extern "C" void SoH3D_Sg_DrawModel(int modelId, const float* mp16, const float* mv16, int lit, int invertY,
                                   unsigned char r8, unsigned char g8, unsigned char b8, unsigned char a8,
                                   float aspectAdj, const float* boneData, int boneCnt, unsigned long long midMask,
                                   int sky, float uvOffU, float uvOffV, const void* matTex) {
    const std::unordered_map<int, int>* matTexMap = static_cast<const std::unordered_map<int, int>*>(matTex);
    if (!g_ctxValid)
        return;
    SgModel* m = ensureUploaded(modelId);
    if (!m || !m->vbo)
        return;
    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;

    // Base UBO shared by all groups (per-group alphaRef/depthOffset/etc. patched below).
    SgUbo base{};
    memcpy(base.uMP, mp16, sizeof(base.uMP));
    base.uMP[0] *= aspectAdj; // mirror Fast3D AdjXForAspectRatio (MP column 0)
    base.uMP[4] *= aspectAdj;
    base.uMP[8] *= aspectAdj;
    base.uMP[12] *= aspectAdj;
    memcpy(base.uMV, mv16, sizeof(base.uMV));
    for (int k = 0; k < 32; k++)
        for (int e = 0; e < 16; e++)
            base.uBones[k * 16 + e] = (e % 5 == 0) ? 1.0f : 0.0f;
    if (boneData && boneCnt > 0) {
        // boneData is row-major (M*v). std140 mat4 is column-major with no transpose-on-upload, so
        // transpose CPU-side to match the GL path (which uploads with GL_TRUE transpose).
        int nb = boneCnt < 32 ? boneCnt : 32;
        for (int k = 0; k < nb; k++) {
            const float* s = boneData + k * 16;
            float* d = base.uBones + k * 16;
            for (int rr = 0; rr < 4; rr++)
                for (int col = 0; col < 4; col++)
                    d[col * 4 + rr] = s[rr * 4 + col];
        }
    }
    base.uParams[0] = invertY ? -1.0f : 1.0f;
    base.uParams[1] = (lit && gSoH3dLightEnable != 0) ? 1.0f : 0.0f;
    base.uTintSkin[0] = r8 / 255.0f;
    base.uTintSkin[1] = g8 / 255.0f;
    base.uTintSkin[2] = b8 / 255.0f;
    base.uTintSkin[3] = (boneData && boneCnt > 0) ? 1.0f : 0.0f;
    base.uLightDir[0] = gSoH3dLightDirWorld[0];
    base.uLightDir[1] = gSoH3dLightDirWorld[1];
    base.uLightDir[2] = gSoH3dLightDirWorld[2];
    base.uLightDir[3] = sky ? 1.0f : 0.0f;
    base.uExtra[0] = a8 / 255.0f;
    base.uExtra[1] = uvOffU;
    base.uExtra[2] = uvOffV;
    // Shadows are M4; off for now (uShadow.x = 0, shadow sampler bound to the dummy texture).
    base.uShadow[0] = 0.0f;
    base.uFog[0] = gSoH3dFogColor[0];
    base.uFog[1] = gSoH3dFogColor[1];
    base.uFog[2] = gSoH3dFogColor[2];
    base.uFog[3] = gSoH3dFogEnable ? 1.0f : 0.0f;
    base.uFog2[0] = gSoH3dFogMul;
    base.uFog2[1] = gSoH3dFogOffset;
    base.uAmbient[0] = gSoH3dWorldAmbColor[0];
    base.uAmbient[1] = gSoH3dWorldAmbColor[1];
    base.uAmbient[2] = gSoH3dWorldAmbColor[2];
    base.uAmbient[3] = 0.0f;
    bool forceBlend = (a8 < 255);

    bool roomHl = (gSoH3dHlGroup >= 0 && m->groups.size() > 20);
    int frontCW = (invertY != 0) ^ (gSoH3dFaceCullFlip != 0);

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
        if (roomHl && gIdx == gSoH3dHlGroup) {
            ubo.uTintSkin[0] = 1.0f;
            ubo.uTintSkin[1] = 0.0f;
            ubo.uTintSkin[2] = 0.0f;
        }
        ubo.uParams[2] = grp.alphaTest ? grp.alphaRef : 0.0f;
        ubo.uParams[3] = grp.polygonOffset;
        ubo.uExtra[3] = (grp.vertexLighting && gSoH3dWorldLit) ? grp.combScaleRGB : 1.0f;
        bool ambGroup = (grp.vertexLighting && gSoH3dWorldLit);
        ubo.uAmbient[0] = base.uAmbient[0] * grp.matAmbient[0];
        ubo.uAmbient[1] = base.uAmbient[1] * grp.matAmbient[1];
        ubo.uAmbient[2] = base.uAmbient[2] * grp.matAmbient[2];
        ubo.uAmbient[3] = ambGroup ? gSoH3dWorldAmb : 0.0f;

        // Facial material-anim override.
        int texIndex = grp.texIndex;
        if (matTexMap && grp.materialIndex >= 0) {
            auto ov = matTexMap->find(grp.materialIndex);
            if (ov != matTexMap->end() && ov->second >= 0)
                texIndex = ov->second;
        }
        SDL_GPUTexture* tex = g_dummyTex;
        SDL_GPUSampler* samp = g_dummySampler;
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
        dg.pipeline = getPipeline(gb, frontCW);
        dg.tex = tex;
        dg.samp = samp;
        dg.first = grp.first;
        dg.count = grp.count;
        memcpy(dg.ubo.data(), &ubo, sizeof(ubo));
        if (dg.pipeline)
            dgs.push_back(std::move(dg));
    }
    if (dgs.empty())
        return;

    SDL_GPUViewport vp{};
    SDL_Rect sc{};
    api->GetSoH3DViewportScissor(vp, sc);
    SDL_GPUBuffer* vbo = m->vbo;
    SDL_GPUTexture* shadowTex = g_dummyTex;
    SDL_GPUSampler* shadowSamp = g_dummySampler;

    // Append ONE op for this model: replayed inside the unified fb pass (interleaves with N64 geom).
    api->AppendSoH3DInPass([vbo, dgs = std::move(dgs), vp, sc, shadowTex,
                            shadowSamp](SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass) {
        SDL_SetGPUViewport(pass, &vp);
        SDL_SetGPUScissor(pass, &sc);
        SDL_GPUBufferBinding vb{};
        vb.buffer = vbo;
        vb.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        for (const DrawGroup& g : dgs) {
            SDL_BindGPUGraphicsPipeline(pass, g.pipeline);
            SDL_PushGPUVertexUniformData(cmd, 0, g.ubo.data(), (uint32_t)g.ubo.size());
            SDL_PushGPUFragmentUniformData(cmd, 0, g.ubo.data(), (uint32_t)g.ubo.size());
            SDL_GPUTextureSamplerBinding sb[2]{};
            sb[0].texture = g.tex;
            sb[0].sampler = g.samp;
            sb[1].texture = shadowTex;
            sb[1].sampler = shadowSamp;
            SDL_BindGPUFragmentSamplers(pass, 0, sb, 2);
            SDL_DrawGPUPrimitives(pass, g.count, 1, g.first, 0);
        }
    });
}

extern "C" void SoH3D_Sg_EndPass(void) {
    g_ctxValid = false;
}

// --- Shadows + AO (M4): not yet ported; no-op so the shared dispatcher skips them cleanly. ---
extern "C" int SoH3D_Sg_BeginShadowPass(void) {
    return 0;
}
extern "C" void SoH3D_Sg_ShadowCasterDraw(int, const float*, const float*, const float*, int, unsigned long long) {
}
extern "C" void SoH3D_Sg_ShadowCasterTris(const float*, size_t, const float*) {
}
extern "C" void SoH3D_Sg_EndShadowPass(void) {
}
extern "C" void SoH3D_Sg_SetShadow(int, const float*) {
}
extern "C" int SoH3D_Sg_BeginDepthPrepass(void) {
    return 0;
}
extern "C" void SoH3D_Sg_DepthPrepassDraw(int, const float*, const float*, int, float, const float*, int,
                                          unsigned long long, int) {
}
extern "C" void SoH3D_Sg_EndDepthPrepass(void) {
}
extern "C" void SoH3D_Sg_AoComposite(void) {
}

#endif // ENABLE_SDL3GPU
