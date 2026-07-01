// RmlUi render interface for the Fast3D SDL3 GPU backend (unified op model). See the header.
#ifdef ENABLE_SDL3GPU

#include "RmlRenderInterfaceSdl3Gpu.h"
#include "fast/backends/gfx_sdl3gpu.h"

#include <RmlUi/Core/Vertex.h>

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <spdlog/spdlog.h>

#include <cstring>
#include <mutex>

using Fast::g_activeSdl3GpuApi;
using Fast::GfxRenderingAPISdl3Gpu;

namespace {

// Pixel-space (origin top-left) + translation -> SDL3 GPU clip. SDL3 GPU NDC is Y-UP, so pixel y=0
// (top) maps to clip y=+1 (the Vulkan interface used Y-down). uTranslate/uViewport via the vertex
// uniform (set 1, binding 0 for SDL3 GPU SPIR-V). RmlUi vertex colours are premultiplied alpha.
const char* kVert = R"(#version 450
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aCol;
layout(location=2) in vec2 aUv;
layout(location=0) out vec4 vCol;
layout(location=1) out vec2 vUv;
layout(set=1, binding=0, std140) uniform UBO { vec2 uTranslate; vec2 uViewport; } ubo;
void main() {
    vec2 p = aPos + ubo.uTranslate;
    gl_Position = vec4(2.0 * p.x / ubo.uViewport.x - 1.0, 1.0 - 2.0 * p.y / ubo.uViewport.y, 0.0, 1.0);
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
    frag = texture(uTex, vUv) * vCol; // both premultiplied
}
)";

struct RmlUbo {
    float uTranslate[2];
    float uViewport[2];
};

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
        SPDLOG_ERROR("[RmlSg] shader parse failed: {}", shader.getInfoLog());
        return false;
    }
    glslang::TProgram prog;
    prog.addShader(&shader);
    if (!prog.link(msg)) {
        SPDLOG_ERROR("[RmlSg] shader link failed: {}", prog.getInfoLog());
        return false;
    }
    glslang::SpvOptions opt;
    opt.disableOptimizer = true;
    glslang::GlslangToSpv(*prog.getIntermediate(stage), spv, &opt);
    return !spv.empty();
}

} // namespace

namespace Ship {

RmlRenderInterfaceSdl3Gpu::RmlRenderInterfaceSdl3Gpu() = default;
RmlRenderInterfaceSdl3Gpu::~RmlRenderInterfaceSdl3Gpu() {
    Shutdown();
}

void RmlRenderInterfaceSdl3Gpu::SetViewport(int w, int h) {
    mViewportW = w > 0 ? w : 1;
    mViewportH = h > 0 ? h : 1;
}

SDL_GPUTexture* RmlRenderInterfaceSdl3Gpu::UploadTexture(const void* rgba, int w, int h) {
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
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(mDevice, &ci);

    const uint32_t size = (uint32_t)w * h * 4;
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(mDevice, &tci);
    void* mapped = SDL_MapGPUTransferBuffer(mDevice, tb, false);
    memcpy(mapped, rgba, size);
    SDL_UnmapGPUTransferBuffer(mDevice, tb);
    SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(mDevice);
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
    SDL_ReleaseGPUTransferBuffer(mDevice, tb);
    return tex;
}

bool RmlRenderInterfaceSdl3Gpu::EnsureResources() {
    if (mReady)
        return true;
    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;
    if (!api)
        return false;
    mDevice = api->GpuDevice();
    if (!mDevice)
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
    mVs = SDL_CreateGPUShader(mDevice, &vci);
    SDL_GPUShaderCreateInfo fci{};
    fci.code_size = fs.size() * sizeof(uint32_t);
    fci.code = (const Uint8*)fs.data();
    fci.entrypoint = "main";
    fci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fci.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fci.num_samplers = 1;
    mFs = SDL_CreateGPUShader(mDevice, &fci);
    if (!mVs || !mFs)
        return false;

    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = SDL_GPU_FILTER_LINEAR;
    si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    mSampler = SDL_CreateGPUSampler(mDevice, &si);

    const unsigned char white[4] = { 255, 255, 255, 255 };
    mWhiteTex = UploadTexture(white, 1, 1);

    // Pick a supported depth-stencil format for the menu's private clip-mask target.
    if (SDL_GPUTextureSupportsFormat(mDevice, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, SDL_GPU_TEXTURETYPE_2D,
                                     SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
        mStencilFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    else
        mStencilFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;

    SDL_GPUVertexAttribute attrs[3]{};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(Rml::Vertex, position) };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, (uint32_t)offsetof(Rml::Vertex, colour) };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(Rml::Vertex, tex_coord) };
    SDL_GPUVertexBufferDescription vb{};
    vb.slot = 0;
    vb.pitch = sizeof(Rml::Vertex);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    // Build one pipeline variant. maskWrite=false: normal draw (premult-alpha blend, colour on,
    // stencil test EQUAL, no stencil write). maskWrite=true: paint the clip mask (no colour, stencil
    // ALWAYS+REPLACE). Both run in the menu's own pass against the private D24S8/D32S8 target.
    auto makePipe = [&](bool maskWrite) -> SDL_GPUGraphicsPipeline* {
        SDL_GPUGraphicsPipelineCreateInfo pci{};
        pci.vertex_shader = mVs;
        pci.fragment_shader = mFs;
        pci.vertex_input_state.vertex_buffer_descriptions = &vb;
        pci.vertex_input_state.num_vertex_buffers = 1;
        pci.vertex_input_state.vertex_attributes = attrs;
        pci.vertex_input_state.num_vertex_attributes = 3;
        pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pci.depth_stencil_state.enable_depth_test = false; // menu always on top
        pci.depth_stencil_state.enable_depth_write = false;
        pci.depth_stencil_state.enable_stencil_test = true;
        pci.depth_stencil_state.compare_mask = 0xFF;
        pci.depth_stencil_state.write_mask = maskWrite ? 0xFF : 0x00;
        SDL_GPUStencilOpState so{};
        so.fail_op = SDL_GPU_STENCILOP_KEEP;
        so.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
        so.pass_op = maskWrite ? SDL_GPU_STENCILOP_REPLACE : SDL_GPU_STENCILOP_KEEP;
        so.compare_op = maskWrite ? SDL_GPU_COMPAREOP_ALWAYS : SDL_GPU_COMPAREOP_EQUAL;
        pci.depth_stencil_state.front_stencil_state = so;
        pci.depth_stencil_state.back_stencil_state = so;
        SDL_GPUColorTargetDescription ct{};
        ct.format = api->GpuColorFormat();
        if (maskWrite) {
            ct.blend_state.enable_color_write_mask = true;
            ct.blend_state.color_write_mask = 0; // mask write: no colour
        } else {
            ct.blend_state.enable_blend = true;
            ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE; // premultiplied alpha
            ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        }
        pci.target_info.color_target_descriptions = &ct;
        pci.target_info.num_color_targets = 1;
        pci.target_info.has_depth_stencil_target = true;
        pci.target_info.depth_stencil_format = mStencilFormat;
        return SDL_CreateGPUGraphicsPipeline(mDevice, &pci);
    };
    mPipeline = makePipe(false);
    mPipelineMask = makePipe(true);
    if (!mPipeline || !mPipelineMask) {
        SPDLOG_ERROR("[RmlSg] pipeline create failed: {}", SDL_GetError());
        return false;
    }

    mReady = true;
    SPDLOG_INFO("[RmlSg] resources ready (unified op model)");
    return true;
}

void RmlRenderInterfaceSdl3Gpu::ProcessPendingFrees() {
    for (auto it = mPendingFrees.begin(); it != mPendingFrees.end();) {
        if (mFrameCounter >= it->freeAtFrame) {
            if (it->geo.vbo)
                SDL_ReleaseGPUBuffer(mDevice, it->geo.vbo);
            if (it->geo.ibo)
                SDL_ReleaseGPUBuffer(mDevice, it->geo.ibo);
            if (it->tex)
                SDL_ReleaseGPUTexture(mDevice, it->tex);
            it = mPendingFrees.erase(it);
        } else {
            ++it;
        }
    }
}

bool RmlRenderInterfaceSdl3Gpu::BeginFrame() {
    mActive = false;
    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;
    if (!api || !api->FrameRecording())
        return false;
    if (!EnsureResources())
        return false;
    mFrameCounter++;
    ProcessPendingFrees();
    mCmds.clear();
    mScissorEnabled = false;
    mClipMaskEnabled = false;
    mStencilRef = 0;
    mStencilCounter = 0;
    mActive = true;
    return true;
}

// (Re)create the menu's private depth-stencil target (depth aspect unused; stencil holds the mask).
bool RmlRenderInterfaceSdl3Gpu::EnsureStencilTarget(int w, int h) {
    if (w <= 0)
        w = 1;
    if (h <= 0)
        h = 1;
    if (mStencilTarget && mStencilW == w && mStencilH == h)
        return true;
    if (mStencilTarget) {
        SDL_WaitForGPUIdle(mDevice);
        SDL_ReleaseGPUTexture(mDevice, mStencilTarget);
        mStencilTarget = nullptr;
    }
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = mStencilFormat;
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    ci.width = (uint32_t)w;
    ci.height = (uint32_t)h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    mStencilTarget = SDL_CreateGPUTexture(mDevice, &ci);
    if (!mStencilTarget) {
        SPDLOG_ERROR("[RmlSg] stencil target create failed: {}", SDL_GetError());
        return false;
    }
    mStencilW = w;
    mStencilH = h;
    return true;
}

void RmlRenderInterfaceSdl3Gpu::EndFrame() {
    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;
    mActive = false;
    if (!api || mCmds.empty())
        return;
    SDL_GPUTexture* fbColor = api->MainFbColorTexture();
    if (!fbColor || !EnsureStencilTarget(mViewportW, mViewportH))
        return;

    int W = mViewportW, H = mViewportH;
    SDL_GPUGraphicsPipeline* pipe = mPipeline;
    SDL_GPUGraphicsPipeline* pipeMask = mPipelineMask;
    SDL_GPUSampler* samp = mSampler;
    SDL_GPUTexture* stencilTarget = mStencilTarget;
    std::vector<Cmd> cmds = std::move(mCmds);

    // The menu draws in its OWN pass that LOADs fb 0's colour and binds a private depth-stencil
    // target cleared to 0 (so the EQUAL test with ref 0 passes everywhere when no clip is active,
    // and Set/Intersect can paint incrementing refs without a mid-pass clear). fb 0's own D32_FLOAT
    // depth is untouched (GetPixelDepth still reads it as raw float).
    api->AppendZelda3DOwnPass([pipe, pipeMask, samp, stencilTarget, fbColor, W, H,
                             cmds = std::move(cmds)](SDL_GPUCommandBuffer* cmd) {
        SDL_GPUColorTargetInfo ct{};
        ct.texture = fbColor;
        ct.load_op = SDL_GPU_LOADOP_LOAD; // composite over the game
        ct.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo dt{};
        dt.texture = stencilTarget;
        dt.clear_depth = 1.0f;
        dt.clear_stencil = 0;
        dt.load_op = SDL_GPU_LOADOP_DONT_CARE; // depth unused (test off)
        dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
        dt.stencil_load_op = SDL_GPU_LOADOP_CLEAR; // stencil mask starts at 0
        dt.stencil_store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
        SDL_GPUViewport vp{ 0.0f, 0.0f, (float)W, (float)H, 0.0f, 1.0f };
        SDL_SetGPUViewport(pass, &vp);
        for (const Cmd& c : cmds) {
            SDL_Rect sc = c.scissorOn ? c.scissor : SDL_Rect{ 0, 0, W, H };
            // Clamp into the framebuffer (SDL3 GPU validates the scissor against the target).
            if (sc.x < 0) {
                sc.w += sc.x;
                sc.x = 0;
            }
            if (sc.y < 0) {
                sc.h += sc.y;
                sc.y = 0;
            }
            if (sc.x > W)
                sc.x = W;
            if (sc.y > H)
                sc.y = H;
            if (sc.x + sc.w > W)
                sc.w = W - sc.x;
            if (sc.y + sc.h > H)
                sc.h = H - sc.y;
            if (sc.w < 0)
                sc.w = 0;
            if (sc.h < 0)
                sc.h = 0;
            SDL_SetGPUScissor(pass, &sc);
            SDL_BindGPUGraphicsPipeline(pass, c.maskWrite ? pipeMask : pipe);
            SDL_SetGPUStencilReference(pass, c.stencilRef);
            RmlUbo ubo{};
            ubo.uTranslate[0] = c.translate[0];
            ubo.uTranslate[1] = c.translate[1];
            ubo.uViewport[0] = (float)W;
            ubo.uViewport[1] = (float)H;
            SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(ubo));
            SDL_GPUBufferBinding vbnd{};
            vbnd.buffer = c.vbo;
            SDL_BindGPUVertexBuffers(pass, 0, &vbnd, 1);
            SDL_GPUBufferBinding ibnd{};
            ibnd.buffer = c.ibo;
            SDL_BindGPUIndexBuffer(pass, &ibnd, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_GPUTextureSamplerBinding sb{};
            sb.texture = c.tex;
            sb.sampler = samp;
            SDL_BindGPUFragmentSamplers(pass, 0, &sb, 1);
            SDL_DrawGPUIndexedPrimitives(pass, c.indexCount, 1, 0, 0, 0);
        }
        SDL_EndGPURenderPass(pass);
    });
}

Rml::CompiledGeometryHandle RmlRenderInterfaceSdl3Gpu::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                                       Rml::Span<const int> indices) {
    if (!mReady || vertices.empty() || indices.empty())
        return 0;

    Geometry g;
    g.indexCount = (uint32_t)indices.size();
    const uint32_t vbBytes = (uint32_t)(vertices.size() * sizeof(Rml::Vertex));
    const uint32_t ibBytes = (uint32_t)(indices.size() * sizeof(int));

    SDL_GPUBufferCreateInfo vbi{};
    vbi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vbi.size = vbBytes;
    g.vbo = SDL_CreateGPUBuffer(mDevice, &vbi);
    SDL_GPUBufferCreateInfo ibi{};
    ibi.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    ibi.size = ibBytes;
    g.ibo = SDL_CreateGPUBuffer(mDevice, &ibi);

    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = vbBytes + ibBytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(mDevice, &tci);
    uint8_t* mapped = (uint8_t*)SDL_MapGPUTransferBuffer(mDevice, tb, false);
    memcpy(mapped, vertices.data(), vbBytes);
    memcpy(mapped + vbBytes, indices.data(), ibBytes);
    SDL_UnmapGPUTransferBuffer(mDevice, tb);
    SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(mDevice);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
    SDL_GPUTransferBufferLocation vsrc{ tb, 0 };
    SDL_GPUBufferRegion vdst{ g.vbo, 0, vbBytes };
    SDL_UploadToGPUBuffer(cp, &vsrc, &vdst, false);
    SDL_GPUTransferBufferLocation isrc{ tb, vbBytes };
    SDL_GPUBufferRegion idst{ g.ibo, 0, ibBytes };
    SDL_UploadToGPUBuffer(cp, &isrc, &idst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(c);
    SDL_ReleaseGPUTransferBuffer(mDevice, tb);

    Rml::CompiledGeometryHandle h = mNextGeometry++;
    mGeometries[h] = g;
    return h;
}

void RmlRenderInterfaceSdl3Gpu::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                                               Rml::TextureHandle texture) {
    if (!mActive)
        return;
    auto git = mGeometries.find(geometry);
    if (git == mGeometries.end())
        return;
    SDL_GPUTexture* tex = mWhiteTex;
    if (texture != 0) {
        auto tit = mTextures.find(texture);
        if (tit != mTextures.end())
            tex = tit->second;
    }
    Cmd c{};
    c.vbo = git->second.vbo;
    c.ibo = git->second.ibo;
    c.indexCount = git->second.indexCount;
    c.tex = tex;
    c.translate[0] = translation.x;
    c.translate[1] = translation.y;
    c.scissorOn = mScissorEnabled;
    c.scissor = mScissor;
    c.maskWrite = false;
    c.stencilRef = mClipMaskEnabled ? mStencilRef : 0; // EQUAL vs ref; 0 + zero-stencil = passes all
    mCmds.push_back(c);
}

void RmlRenderInterfaceSdl3Gpu::EnableClipMask(bool enable) {
    // The normal pipeline always tests stencil EQUAL. Disabled -> ref 0 + zeroed stencil passes
    // everywhere; enabled -> ref is the value the last RenderToClipMask painted.
    mClipMaskEnabled = enable;
    if (!enable)
        mStencilRef = 0;
}

void RmlRenderInterfaceSdl3Gpu::RenderToClipMask(Rml::ClipMaskOperation operation,
                                                 Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) {
    if (!mActive)
        return;
    auto git = mGeometries.find(geometry);
    if (git == mGeometries.end() || git->second.indexCount == 0)
        return;

    // SDL3 GPU can't clear the stencil mid-pass, so instead of Vulkan's clear-then-paint we paint
    // each region with a fresh, monotonically-incrementing ref via ALWAYS+REPLACE. Normal draws then
    // test EQUAL against that ref, so only this region's pixels pass — old regions hold older refs
    // that no longer match. (Set/Intersect collapse to the same scheme, matching the Vulkan path's
    // approximate intersect. SetInverse — rare — has no no-clear equivalent, so it leaves no mask.)
    if (operation == Rml::ClipMaskOperation::SetInverse) {
        mStencilRef = 0; // unsupported without a stencil clear: treat as no clip
        return;
    }
    uint8_t writeRef = ++mStencilCounter;
    if (writeRef == 0) // wrapped past 255 (only on an implausibly clip-heavy frame)
        writeRef = mStencilCounter = 1;
    mStencilRef = writeRef;

    Cmd c{};
    c.vbo = git->second.vbo;
    c.ibo = git->second.ibo;
    c.indexCount = git->second.indexCount;
    c.tex = mWhiteTex;
    c.translate[0] = translation.x;
    c.translate[1] = translation.y;
    c.scissorOn = mScissorEnabled;
    c.scissor = mScissor;
    c.maskWrite = true;
    c.stencilRef = writeRef;
    mCmds.push_back(c);
}

void RmlRenderInterfaceSdl3Gpu::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    auto it = mGeometries.find(geometry);
    if (it == mGeometries.end())
        return;
    // Defer the GPU-buffer free until this frame's recorded op cannot still reference it.
    PendingFree pf{};
    pf.freeAtFrame = mFrameCounter + 3;
    pf.geo = it->second;
    mPendingFrees.push_back(pf);
    mGeometries.erase(it);
}

Rml::TextureHandle RmlRenderInterfaceSdl3Gpu::LoadTexture(Rml::Vector2i& texture_dimensions,
                                                          const Rml::String& source) {
    // Menu styling uses generated (font) textures + solid colours; image-file decorators are not
    // used by the curated menu. Return 0 (untextured/white) rather than pull in an image loader.
    (void)source;
    texture_dimensions = Rml::Vector2i(1, 1);
    return 0;
}

Rml::TextureHandle RmlRenderInterfaceSdl3Gpu::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                              Rml::Vector2i source_dimensions) {
    if (!mReady)
        return 0;
    SDL_GPUTexture* tex = UploadTexture(source.data(), source_dimensions.x, source_dimensions.y);
    Rml::TextureHandle h = mNextTexture++;
    mTextures[h] = tex;
    return h;
}

void RmlRenderInterfaceSdl3Gpu::ReleaseTexture(Rml::TextureHandle texture) {
    auto it = mTextures.find(texture);
    if (it == mTextures.end())
        return;
    PendingFree pf{};
    pf.freeAtFrame = mFrameCounter + 3;
    pf.tex = it->second;
    mPendingFrees.push_back(pf);
    mTextures.erase(it);
}

void RmlRenderInterfaceSdl3Gpu::EnableScissorRegion(bool enable) {
    mScissorEnabled = enable;
}

void RmlRenderInterfaceSdl3Gpu::SetScissorRegion(Rml::Rectanglei region) {
    mScissor.x = region.Left();
    mScissor.y = region.Top();
    mScissor.w = region.Width() < 0 ? 0 : region.Width();
    mScissor.h = region.Height() < 0 ? 0 : region.Height();
}

Rml::LayerHandle RmlRenderInterfaceSdl3Gpu::PushLayer() {
    // No offscreen layer pool (filters deferred); render straight to fb 0. Hand back a unique handle.
    return mNextLayer++;
}

void RmlRenderInterfaceSdl3Gpu::Shutdown() {
    if (!mDevice)
        return;
    SDL_WaitForGPUIdle(mDevice);
    for (auto& kv : mGeometries) {
        if (kv.second.vbo)
            SDL_ReleaseGPUBuffer(mDevice, kv.second.vbo);
        if (kv.second.ibo)
            SDL_ReleaseGPUBuffer(mDevice, kv.second.ibo);
    }
    mGeometries.clear();
    for (auto& kv : mTextures)
        if (kv.second)
            SDL_ReleaseGPUTexture(mDevice, kv.second);
    mTextures.clear();
    for (auto& pf : mPendingFrees) {
        if (pf.geo.vbo)
            SDL_ReleaseGPUBuffer(mDevice, pf.geo.vbo);
        if (pf.geo.ibo)
            SDL_ReleaseGPUBuffer(mDevice, pf.geo.ibo);
        if (pf.tex)
            SDL_ReleaseGPUTexture(mDevice, pf.tex);
    }
    mPendingFrees.clear();
    if (mStencilTarget)
        SDL_ReleaseGPUTexture(mDevice, mStencilTarget);
    if (mWhiteTex)
        SDL_ReleaseGPUTexture(mDevice, mWhiteTex);
    if (mSampler)
        SDL_ReleaseGPUSampler(mDevice, mSampler);
    if (mPipeline)
        SDL_ReleaseGPUGraphicsPipeline(mDevice, mPipeline);
    if (mPipelineMask)
        SDL_ReleaseGPUGraphicsPipeline(mDevice, mPipelineMask);
    if (mVs)
        SDL_ReleaseGPUShader(mDevice, mVs);
    if (mFs)
        SDL_ReleaseGPUShader(mDevice, mFs);
    mStencilTarget = nullptr;
    mStencilW = mStencilH = 0;
    mWhiteTex = nullptr;
    mSampler = nullptr;
    mPipeline = nullptr;
    mPipelineMask = nullptr;
    mVs = mFs = nullptr;
    mReady = false;
    mDevice = nullptr;
}

} // namespace Ship

#endif // ENABLE_SDL3GPU
