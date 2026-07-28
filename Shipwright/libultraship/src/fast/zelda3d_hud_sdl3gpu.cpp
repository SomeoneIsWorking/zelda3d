// Zelda3D HUD — SDL3 GPU immediate-mode 2D textured-quad renderer, on the UNIFIED op model.
//
// The quads are COLLECTED during the Zelda3D_Hud_Begin..End bracket and each coalesced run is appended as a
// first-class OP_DRAW into the SDL3 GPU backend's deferred op-list (AppendZelda3DHudDraw), targeting
// framebuffer 0 (the composited frame the present blit / headless readback uses), so the HUD replays
// on top of the N64 + OoT3D model content in the same single render pass through the same bind path.
// The public C-ABI (Zelda3D_Hud_*) at the bottom of this file is what the soh-side HUD module
// (soh/src/zelda3d/hud/zelda3d_hud.cpp) draws through.
#ifdef ENABLE_SDL3GPU

#include "fast/zelda3d_hud_sdl3gpu.h"
#include "fast/backends/gfx_sdl3gpu.h"
#include "fast/backends/zelda3d_sdl3gpu.h" // Fast::Zelda3DHudRenderer (this module's state, folded into the backend)

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
// y=0 (top) maps to clip y=+1: gl_Position.y = 1 - 2*y/H. Viewport size arrives via a vertex
// uniform (set 1, binding 0 for SDL3 GPU SPIR-V).
const char* kVert = R"(#version 450
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aCol;
layout(location=2) in vec2 aUv;
layout(location=3) in vec4 aEnvMode;
layout(location=0) out vec4 vCol;
layout(location=1) out vec2 vUv;
layout(location=2) out vec4 vEnvMode;
layout(set=1, binding=0, std140) uniform UBO { vec2 uViewport; } ubo;
void main() {
    gl_Position = vec4(2.0 * aPos.x / ubo.uViewport.x - 1.0, 1.0 - 2.0 * aPos.y / ubo.uViewport.y, 0.0, 1.0);
    vCol = aCol;
    vUv = aUv;
    vEnvMode = aEnvMode;
}
)";

// aEnvMode.rgb is the N64 ENV colour; aEnvMode.a is the combine MODE (see HudVert). Carrying the
// mode per-vertex rather than per-pipeline keeps every HUD quad in one pipeline, so runs still
// coalesce by texture alone.
const char* kFrag = R"(#version 450
layout(location=0) in vec4 vCol;
layout(location=1) in vec2 vUv;
layout(location=2) in vec4 vEnvMode;
layout(location=0) out vec4 frag;
layout(set=2, binding=0) uniform sampler2D uTex;
void main() {
    vec4 t = texture(uTex, vUv);
    // vEnvMode.a carries the mode as 0 / 0.5 / 1 (0, 128, 255 as a normalised byte).
    if (vEnvMode.a > 0.75) {
        // Magic-bar fill: rgb lerps like the heart combine but alpha comes from PRIMITIVE alone.
        frag = vec4(mix(vEnvMode.rgb, vCol.rgb, t.r), vCol.a);
    } else if (vEnvMode.a > 0.25) {
        // z_lifemeter.c's heart combine: (PRIM - ENV) * TEXEL0 + ENV on rgb, TEXEL0.a * PRIM.a on
        // alpha. Our heart textures are grayscale-intensity + silhouette alpha, so .r is the factor.
        frag = vec4(mix(vEnvMode.rgb, vCol.rgb, t.r), t.a * vCol.a);
    } else {
        frag = t * vCol; // straight alpha
    }
}
)";

// HudVert, DrawRun, HudSg and the kMaxQuadsPerFrame/kVertsPerQuad/kRingFrames constants now live in
// fast/backends/zelda3d_sdl3gpu.h (so they can be members of Fast::Zelda3DHudRenderer). The vertex shader's
// viewport UBO ({ vec2 uViewport; vec2 pad; }) is now built by the backend (AppendZelda3DHudDraw).

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

} // namespace

// uploadTex / ensureResources and the SgHud_* collector below are now methods of
// Fast::Zelda3DHudRenderer (the former file-global `HudSg g` + `g_idToTex` + `g_nextId` are members).
namespace Fast {

SDL_GPUTexture* Zelda3DHudRenderer::uploadTex(const void* rgba, int w, int h) {
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

bool Zelda3DHudRenderer::ensureResources() {
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
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    g.samplerRepeat = SDL_CreateGPUSampler(g.device, &si);

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

    SDL_GPUVertexAttribute attrs[4]{};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(HudVert, x) };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, (uint32_t)offsetof(HudVert, r) };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(HudVert, u) };
    attrs[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, (uint32_t)offsetof(HudVert, er) };
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
    pci.vertex_input_state.num_vertex_attributes = 4;
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

int Zelda3DHudRenderer::Available() {
    return g_activeSdl3GpuApi != nullptr ? 1 : 0;
}

int Zelda3DHudRenderer::Begin(int* outW, int* outH) {
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

// g_idToTex (per-frame id->tex table) + g_nextId are members of Zelda3DHudRenderer. The persistent
// texCache (keyed by the stable source-RGBA pointer) avoids re-uploading each frame.
int Zelda3DHudRenderer::Tex(const void* key, const void* rgba, int w, int h) {
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

void Zelda3DHudRenderer::Draw(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                            unsigned int tintRGBA, unsigned int envRGB, int mode) {
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
    const uint8_t er = (uint8_t)((envRGB >> 16) & 0xFF);
    const uint8_t eg = (uint8_t)((envRGB >> 8) & 0xFF);
    const uint8_t eb = (uint8_t)(envRGB & 0xFF);
    const uint8_t md = (uint8_t)(mode >= 2 ? 255 : (mode == 1 ? 128 : 0));
    const float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    HudVert quad[6] = {
        { x0, y0, cr, cg, cb, ca, u0, v0, er, eg, eb, md }, { x1, y0, cr, cg, cb, ca, u1, v0, er, eg, eb, md },
        { x0, y1, cr, cg, cb, ca, u0, v1, er, eg, eb, md }, { x1, y0, cr, cg, cb, ca, u1, v0, er, eg, eb, md },
        { x1, y1, cr, cg, cb, ca, u1, v1, er, eg, eb, md }, { x0, y1, cr, cg, cb, ca, u0, v1, er, eg, eb, md },
    };
    // A quad whose UVs leave [0,1] means to TILE its texture (the magic bar repeats a 24px strip
    // across the whole bar), so it needs the repeat sampler; everything else clamps.
    SDL_GPUSampler* samp = (u0 < 0.0f || v0 < 0.0f || u1 > 1.0f || v1 > 1.0f) ? g.samplerRepeat : g.sampler;
    uint32_t first = (uint32_t)g.verts.size();
    g.verts.insert(g.verts.end(), quad, quad + 6);
    // Coalesce consecutive quads that share a texture and a sampler into one draw run.
    if (!g.runs.empty() && g.runs.back().tex == view && g.runs.back().sampler == samp &&
        g.runs.back().firstVert + g.runs.back().vertCount == first) {
        g.runs.back().vertCount += kVertsPerQuad;
    } else {
        g.runs.push_back({ view, samp, first, kVertsPerQuad });
    }
}

void Zelda3DHudRenderer::End() {
    Flush();
    g.active = false;
    g_idToTex.clear();
    g_nextId = 1;
}

// Upload + append what is collected so far, then keep collecting. Every flush takes its own ring
// slot, so a frame consumes one slot per converted element rather than one in total.
void Zelda3DHudRenderer::Flush() {
    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;
    if (!g.active || !api)
        return;
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

    // Append each coalesced quad-run as a first-class OP_DRAW into fb 0 — replayed on top of the N64 +
    // OoT3D model content in the unified pass, through the backend's single fragment-sampler bind path.
    // Run order is preserved by sequential append.
    for (const DrawRun& r : g.runs)
        api->AppendZelda3DHudDraw(g.pipeline, ring.vbo, r.firstVert, r.vertCount, r.tex, r.sampler, (float)g.w,
                                (float)g.h);

    // This batch is now owned by the op list; start a fresh one. The id->tex table stays valid — a
    // texture id handed out before a flush must still resolve for quads recorded after it.
    g.verts.clear();
    g.runs.clear();
}

} // namespace Fast

// ---- Public C-ABI (Zelda3D_Hud_*) ----------------------------------------------------------------
// SDL3 GPU is the only renderer, so these delegate straight to the op-emitting collector above.
// Called from soh/src/zelda3d/hud/zelda3d_hud.cpp (Zelda3D_HudFrame), which libultraship's
// Gui::EndFrame invokes once per frame.
extern "C" {

// Thin shims forwarding to the Zelda3DHudRenderer member subsystem on the live backend (null -> no-op /
// 0, matching the former FrameRecording/api null guards).
static inline Fast::Zelda3DHudRenderer* hudR() {
    return Fast::g_activeSdl3GpuApi ? Fast::g_activeSdl3GpuApi->Hud() : nullptr;
}

int Zelda3D_Hud_Available(void) {
    auto* r = hudR();
    return r ? r->Available() : 0;
}

int Zelda3D_Hud_Begin(int* outW, int* outH) {
    auto* r = hudR();
    return r ? r->Begin(outW, outH) : 0;
}

int Zelda3D_Hud_Tex(const void* key, const void* rgba, int w, int h) {
    auto* r = hudR();
    return r ? r->Tex(key, rgba, w, h) : 0;
}

void Zelda3D_Hud_Draw(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                    unsigned int tintRGBA) {
    if (auto* r = hudR())
        r->Draw(tex, x, y, w, h, u0, v0, u1, v1, tintRGBA, 0u, 0);
}

void Zelda3D_Hud_DrawEnv(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                       unsigned int tintRGBA, unsigned int envRGB, int mode) {
    if (auto* r = hudR())
        r->Draw(tex, x, y, w, h, u0, v0, u1, v1, tintRGBA, envRGB, mode);
}

void Zelda3D_Hud_End(void) {
    if (auto* r = hudR())
        r->End();
}

void Zelda3D_Hud_Flush(void) {
    if (auto* r = hudR())
        r->Flush();
}

void Zelda3D_Hud_Shutdown(void) {
    // SDL3 GPU HUD resources are owned by the backend device and torn down with it; nothing to do.
}

} // extern "C"

#else // !ENABLE_SDL3GPU — stubs so the soh-side HUD links if the backend is somehow disabled.

extern "C" {
int Zelda3D_Hud_Available(void) {
    return 0;
}
int Zelda3D_Hud_Begin(int* outW, int* outH) {
    (void)outW;
    (void)outH;
    return 0;
}
int Zelda3D_Hud_Tex(const void* key, const void* rgba, int w, int h) {
    (void)key;
    (void)rgba;
    (void)w;
    (void)h;
    return 0;
}
void Zelda3D_Hud_Draw(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                    unsigned int tintRGBA) {
    (void)tex;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)u0;
    (void)v0;
    (void)u1;
    (void)v1;
    (void)tintRGBA;
}
void Zelda3D_Hud_DrawEnv(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                       unsigned int tintRGBA, unsigned int envRGB, int mode) {
    (void)tex;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)u0;
    (void)v0;
    (void)u1;
    (void)v1;
    (void)tintRGBA;
    (void)envRGB;
    (void)mode;
}
void Zelda3D_Hud_End(void) {
}
void Zelda3D_Hud_Flush(void) {
}
void Zelda3D_Hud_Shutdown(void) {
}
}

#endif // ENABLE_SDL3GPU
