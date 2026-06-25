// SoH3D PC HUD — SDL3 GPU immediate-mode 2D textured-quad renderer, on the UNIFIED op model.
//
// The Vulkan twin (soh3d_hud_vk.cpp) recorded its quads into a live command buffer obtained via
// BeginSoH3DPass. Here the quads are COLLECTED during the SoH3D_Hud_Begin..End bracket and appended
// as ONE op into the SDL3 GPU backend's deferred op-list, targeting framebuffer 0 (the composited
// frame the present blit / headless readback uses), so the HUD replays on top of the N64 + OoT3D
// model content in the same single render pass. soh3d_hud_vk.cpp's extern "C" SoH3D_Hud_* entry
// points delegate here when the SDL3 GPU backend is the live one.
#ifdef ENABLE_SDL3GPU

#include "fast/soh3d_hud_sdl3gpu.h"
#include "fast/backends/gfx_sdl3gpu.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

using Fast::g_activeSdl3GpuApi;
using Fast::GfxRenderingAPISdl3Gpu;

namespace {

// Pixel-space (origin top-left) -> SDL3 GPU clip. SDL3 GPU NDC is Y-UP (GL convention), so pixel
// y=0 (top) maps to clip y=+1: gl_Position.y = 1 - 2*y/H. (The Vulkan HUD used 2*y/H-1 because its
// NDC is Y-down.) Viewport size arrives via a vertex uniform (set 1, binding 0 for SDL3 GPU SPIR-V).
const char* kVert = R"(#version 450
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aCol;
layout(location=2) in vec2 aUv;
layout(location=0) out vec4 vCol;
layout(location=1) out vec2 vUv;
layout(set=1, binding=0, std140) uniform UBO { vec2 uViewport; } ubo;
void main() {
    gl_Position = vec4(2.0 * aPos.x / ubo.uViewport.x - 1.0, 1.0 - 2.0 * aPos.y / ubo.uViewport.y, 0.0, 1.0);
    vCol = aCol;
    vUv = aUv;
}
)";

const char* kFrag = R"(#version 450
layout(location=0) in vec4 vCol;
layout(location=1) in vec2 vUv;
layout(location=0) out vec4 frag;
layout(set=2, binding=0) uniform sampler2D uTex;
void main() {
    frag = texture(uTex, vUv) * vCol; // straight alpha
}
)";

struct HudVert {
    float x, y;
    uint8_t r, g, b, a;
    float u, v;
};
struct HudUbo {
    float viewport[2];
    float pad[2];
};

constexpr uint32_t kMaxQuadsPerFrame = 2048;
constexpr uint32_t kVertsPerQuad = 6;
constexpr uint32_t kRingFrames = 3;

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
        fprintf(stderr, "[HudSg] shader parse failed: %s\n", shader.getInfoLog());
        return false;
    }
    glslang::TProgram prog;
    prog.addShader(&shader);
    if (!prog.link(msg)) {
        fprintf(stderr, "[HudSg] shader link failed: %s\n", prog.getInfoLog());
        return false;
    }
    glslang::SpvOptions opt;
    opt.disableOptimizer = true;
    glslang::GlslangToSpv(*prog.getIntermediate(stage), spv, &opt);
    return !spv.empty();
}

// One contiguous run of quads sharing a texture (a single SDL_DrawGPUPrimitives).
struct DrawRun {
    SDL_GPUTexture* tex;
    uint32_t firstVert, vertCount;
};

struct HudSg {
    SDL_GPUDevice* device = nullptr;
    bool ready = false;
    SDL_GPUShader* vs = nullptr;
    SDL_GPUShader* fs = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    SDL_GPUTexture* whiteTex = nullptr;

    // Per-frame vertex ring (host transfer + device vertex buffer).
    struct Ring {
        SDL_GPUTransferBuffer* transfer = nullptr;
        SDL_GPUBuffer* vbo = nullptr;
    } rings[kRingFrames];
    uint32_t ringIdx = 0;

    std::unordered_map<const void*, SDL_GPUTexture*> texCache;

    // Collected this frame.
    bool active = false;
    int w = 0, h = 0;
    std::vector<HudVert> verts;
    std::vector<DrawRun> runs;
};

HudSg g;

SDL_GPUTexture* uploadTex(const void* rgba, int w, int h) {
    if (w <= 0)
        w = 1;
    if (h <= 0)
        h = 1;
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = (uint32_t)w;
    ci.height = (uint32_t)h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(g.device, &ci);

    const uint32_t size = (uint32_t)w * h * 4;
    static const unsigned char white[4] = { 255, 255, 255, 255 };
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = rgba ? size : 4;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(g.device, &tci);
    void* mapped = SDL_MapGPUTransferBuffer(g.device, tb, false);
    memcpy(mapped, rgba ? rgba : (const void*)white, rgba ? size : 4);
    SDL_UnmapGPUTransferBuffer(g.device, tb);
    SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(g.device);
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
    SDL_ReleaseGPUTransferBuffer(g.device, tb);
    return tex;
}

bool ensureResources() {
    if (g.ready)
        return true;
    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;
    if (!api)
        return false;
    g.device = api->GpuDevice();
    if (!g.device)
        return false;

    std::vector<uint32_t> vs, fs;
    if (!CompileGlsl(EShLangVertex, kVert, vs) || !CompileGlsl(EShLangFragment, kFrag, fs))
        return false;
    SDL_GPUShaderCreateInfo vci{};
    vci.code_size = vs.size() * sizeof(uint32_t);
    vci.code = (const Uint8*)vs.data();
    vci.entrypoint = "main";
    vci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vci.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vci.num_uniform_buffers = 1;
    g.vs = SDL_CreateGPUShader(g.device, &vci);
    SDL_GPUShaderCreateInfo fci{};
    fci.code_size = fs.size() * sizeof(uint32_t);
    fci.code = (const Uint8*)fs.data();
    fci.entrypoint = "main";
    fci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fci.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fci.num_samplers = 1;
    g.fs = SDL_CreateGPUShader(g.device, &fci);
    if (!g.vs || !g.fs)
        return false;

    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = SDL_GPU_FILTER_LINEAR;
    si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    g.sampler = SDL_CreateGPUSampler(g.device, &si);

    g.whiteTex = uploadTex(nullptr, 1, 1);

    for (uint32_t i = 0; i < kRingFrames; i++) {
        SDL_GPUTransferBufferCreateInfo tci{};
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tci.size = kMaxQuadsPerFrame * kVertsPerQuad * sizeof(HudVert);
        g.rings[i].transfer = SDL_CreateGPUTransferBuffer(g.device, &tci);
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bci.size = kMaxQuadsPerFrame * kVertsPerQuad * sizeof(HudVert);
        g.rings[i].vbo = SDL_CreateGPUBuffer(g.device, &bci);
    }

    SDL_GPUVertexAttribute attrs[3]{};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(HudVert, x) };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, (uint32_t)offsetof(HudVert, r) };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(HudVert, u) };
    SDL_GPUVertexBufferDescription vb{};
    vb.slot = 0;
    vb.pitch = sizeof(HudVert);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = g.vs;
    pci.fragment_shader = g.fs;
    pci.vertex_input_state.vertex_buffer_descriptions = &vb;
    pci.vertex_input_state.num_vertex_buffers = 1;
    pci.vertex_input_state.vertex_attributes = attrs;
    pci.vertex_input_state.num_vertex_attributes = 3;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pci.depth_stencil_state.enable_depth_test = false; // HUD always on top
    pci.depth_stencil_state.enable_depth_write = false;
    SDL_GPUColorTargetDescription ct{};
    ct.format = api->GpuColorFormat();
    ct.blend_state.enable_blend = true;
    ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true; // fb 0's pass binds a depth target
    pci.target_info.depth_stencil_format = api->GpuDepthFormat();
    g.pipeline = SDL_CreateGPUGraphicsPipeline(g.device, &pci);
    if (!g.pipeline) {
        fprintf(stderr, "[HudSg] pipeline create failed: %s\n", SDL_GetError());
        return false;
    }

    g.ready = true;
    fprintf(stderr, "[HudSg] resources ready (unified op model)\n");
    return true;
}

} // namespace

namespace Fast {

int SgHud_Available() {
    return g_activeSdl3GpuApi != nullptr ? 1 : 0;
}

int SgHud_Begin(int* outW, int* outH) {
    g.active = false;
    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;
    if (!api || !api->FrameRecording())
        return 0;
    if (!ensureResources())
        return 0;
    api->MainFbSize(g.w, g.h);
    if (g.w <= 0 || g.h <= 0)
        return 0;
    g.verts.clear();
    g.runs.clear();
    g.active = true;
    if (outW)
        *outW = g.w;
    if (outH)
        *outH = g.h;
    return 1;
}

// The Draw ABI takes an int id; resolve the texture at Draw time from a per-frame id->tex table.
// The persistent texCache (keyed by the stable source-RGBA pointer) avoids re-uploading each frame.
std::unordered_map<int, SDL_GPUTexture*> g_idToTex;
int g_nextId = 1;

int SgHud_Tex(const void* key, const void* rgba, int w, int h) {
    if (!g.active || !key)
        return 0;
    auto it = g.texCache.find(key);
    SDL_GPUTexture* t;
    if (it != g.texCache.end()) {
        t = it->second;
    } else {
        t = uploadTex(rgba, w, h);
        g.texCache[key] = t;
    }
    int id = g_nextId++;
    g_idToTex[id] = t;
    return id;
}

void SgHud_Draw(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                unsigned int tintRGBA) {
    if (!g.active)
        return;
    if (g.verts.size() + kVertsPerQuad > kMaxQuadsPerFrame * kVertsPerQuad)
        return;
    SDL_GPUTexture* view = g.whiteTex;
    if (tex != 0) {
        auto t = g_idToTex.find(tex);
        if (t != g_idToTex.end())
            view = t->second;
    }
    const uint8_t cr = (uint8_t)((tintRGBA >> 24) & 0xFF);
    const uint8_t cg = (uint8_t)((tintRGBA >> 16) & 0xFF);
    const uint8_t cb = (uint8_t)((tintRGBA >> 8) & 0xFF);
    const uint8_t ca = (uint8_t)(tintRGBA & 0xFF);
    const float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    HudVert quad[6] = {
        { x0, y0, cr, cg, cb, ca, u0, v0 }, { x1, y0, cr, cg, cb, ca, u1, v0 },
        { x0, y1, cr, cg, cb, ca, u0, v1 }, { x1, y0, cr, cg, cb, ca, u1, v0 },
        { x1, y1, cr, cg, cb, ca, u1, v1 }, { x0, y1, cr, cg, cb, ca, u0, v1 },
    };
    uint32_t first = (uint32_t)g.verts.size();
    g.verts.insert(g.verts.end(), quad, quad + 6);
    // Coalesce consecutive quads that share a texture into one draw run.
    if (!g.runs.empty() && g.runs.back().tex == view &&
        g.runs.back().firstVert + g.runs.back().vertCount == first) {
        g.runs.back().vertCount += kVertsPerQuad;
    } else {
        g.runs.push_back({ view, first, kVertsPerQuad });
    }
}

void SgHud_End() {
    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;
    if (!g.active || !api) {
        g.active = false;
        return;
    }
    g.active = false;
    g_idToTex.clear();
    g_nextId = 1;
    if (g.verts.empty() || g.runs.empty())
        return;

    // Upload this frame's quad vertices to a ring vertex buffer (private command buffer; SDL3 GPU
    // tracks the hazard so the FinishRender pass sees them). Cycle through kRingFrames buffers so a
    // still-in-flight frame's vertices are not overwritten.
    HudSg::Ring& ring = g.rings[g.ringIdx];
    g.ringIdx = (g.ringIdx + 1) % kRingFrames;
    const uint32_t bytes = (uint32_t)(g.verts.size() * sizeof(HudVert));
    void* mapped = SDL_MapGPUTransferBuffer(g.device, ring.transfer, true /*cycle*/);
    memcpy(mapped, g.verts.data(), bytes);
    SDL_UnmapGPUTransferBuffer(g.device, ring.transfer);
    SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(g.device);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = ring.transfer;
    SDL_GPUBufferRegion dst{};
    dst.buffer = ring.vbo;
    dst.size = bytes;
    SDL_UploadToGPUBuffer(cp, &src, &dst, true /*cycle*/);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(c);

    HudUbo ubo{};
    ubo.viewport[0] = (float)g.w;
    ubo.viewport[1] = (float)g.h;
    SDL_GPUBuffer* vbo = ring.vbo;
    SDL_GPUGraphicsPipeline* pipe = g.pipeline;
    SDL_GPUSampler* samp = g.sampler;
    int W = g.w, H = g.h;
    std::vector<DrawRun> runs = std::move(g.runs);

    // ONE op into fb 0: replayed on top of the N64 + OoT3D model content in the unified pass.
    api->AppendSoH3DInPassFb(0, [vbo, pipe, samp, ubo, W, H, runs = std::move(runs)](
                                    SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass) {
        SDL_GPUViewport vp{ 0.0f, 0.0f, (float)W, (float)H, 0.0f, 1.0f };
        SDL_Rect sc{ 0, 0, W, H };
        SDL_BindGPUGraphicsPipeline(pass, pipe);
        SDL_SetGPUViewport(pass, &vp);
        SDL_SetGPUScissor(pass, &sc);
        SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(ubo));
        SDL_GPUBufferBinding vbnd{};
        vbnd.buffer = vbo;
        SDL_BindGPUVertexBuffers(pass, 0, &vbnd, 1);
        for (const DrawRun& r : runs) {
            SDL_GPUTextureSamplerBinding sb{};
            sb.texture = r.tex;
            sb.sampler = samp;
            SDL_BindGPUFragmentSamplers(pass, 0, &sb, 1);
            SDL_DrawGPUPrimitives(pass, r.vertCount, 1, r.firstVert, 0);
        }
    });
}

} // namespace Fast

#endif // ENABLE_SDL3GPU
