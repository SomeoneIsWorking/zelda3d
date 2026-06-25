#ifdef ENABLE_SDL3GPU
#pragma once

#include "gfx_rendering_api.h"
#include "../interpreter.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <vector>
#include <map>
#include <array>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <functional>

namespace Fast {

// Forward declaration of the SDL window backend so the SDL3-GPU rendering API can pull the
// SDL_Window out of it (to claim it for the GPU device). gfx_sdl2.cpp is the shared window
// manager for GL / Metal / Vulkan / SDL3-GPU on SDL.
class GfxWindowBackendSDL2;

// Per-combiner shader record. Holds the compiled SPIR-V shader objects and the vertex-input
// layout derived from the color-combiner features, mirroring ShaderProgramVulkan. Pipelines are
// built lazily (per render-state combo) and cached separately, keyed on (id0,id1,stateBits).
//
// Named *SDL3 (not the interface's opaque Fast::ShaderProgram) because multiple backends compile
// in the same build (GL + Vulkan + SDL3-GPU on Linux); only one header may define
// Fast::ShaderProgram (gfx_opengl.h does). We treat ShaderProgram* opaquely and cast, exactly like
// gfx_metal's ShaderProgramMetal / gfx_vulkan's ShaderProgramVulkan.
struct ShaderProgramSDL3 {
    uint64_t id0 = 0, id1 = 0;
    uint8_t numInputs = 0;
    bool usedTextures[2] = { false, false };
    uint8_t numFloats = 0; // vertex stride in floats
    SDL_GPUShader* vert = nullptr;
    SDL_GPUShader* frag = nullptr;
    // Which of the 6 sampler slots (tex0,tex1,mask0,mask1,blend0,blend1) the fragment shader uses.
    bool usedSlot[6] = { false, false, false, false, false, false };
    uint32_t numSamplers = 0; // count of used slots == fragment shader's num_samplers
    // Vertex input attributes in declaration order (location == index).
    struct Attr {
        uint32_t size;   // component count (1..4 floats)
        uint32_t offset; // byte offset within the vertex
    };
    std::vector<Attr> attribs;
};

// Pipeline cache key: shader (combiner) plus the render-state knobs that affect pipeline creation
// (depth test/mask, zmode-decal depth-bias, blend). SDL3 GPU has no dynamic depth bias, so
// zmodeDecal is baked into the pipeline variant (it is already a stateBit). Viewport/scissor remain
// dynamic. Same key shape as VulkanPipelineKey.
struct Sdl3PipelineKey {
    uint64_t id0, id1;
    uint32_t stateBits; // bit0 depthTest, bit1 depthMask, bit2 zmodeDecal, bit3 useAlpha
    bool operator==(const Sdl3PipelineKey& o) const {
        return id0 == o.id0 && id1 == o.id1 && stateBits == o.stateBits;
    }
};
struct Sdl3PipelineKeyHash {
    size_t operator()(const Sdl3PipelineKey& k) const {
        return std::hash<uint64_t>()(k.id0) ^ (std::hash<uint64_t>()(k.id1) << 1) ^
               (std::hash<uint32_t>()(k.stateBits) << 2);
    }
};

// A GPU texture plus its sampling metadata.
struct TextureSDL3 {
    SDL_GPUTexture* tex = nullptr;
    uint32_t width = 0, height = 0;
    uint16_t filtering = 0; // FILTER_* (for the in-shader three-point path)
    bool linearFilter = false;
    uint32_t cms = 0, cmt = 0;
    bool uploaded = false;
    // When true this entry does NOT own its texture — it aliases a framebuffer's color texture so
    // the combiner draw path can sample an FB as a texture (SelectTextureFb). Skipped by
    // DeleteTexture / teardown.
    bool isFbAlias = false;
};

// An offscreen render target: a color texture (+ optional depth). SDL3 GPU has no standalone
// renderpass/framebuffer objects — the targets are bound at SDL_BeginGPURenderPass. fb id 0 is the
// "main" framebuffer the whole frame composites into; FinishRender blits its color onto the
// acquired swapchain texture to present. Higher ids are the interpreter's intermediate effect
// buffers. Single-sample only for now.
struct FramebufferSDL3 {
    uint32_t width = 0, height = 0;
    bool hasDepth = false;
    bool invertY = false; // OpenGL-style bottom-left origin flag (for Copy/Read Y handling)
    bool renderTarget = false;
    SDL_GPUTexture* color = nullptr;
    SDL_GPUTexture* depth = nullptr;
    uint32_t colorTexId = 0; // mTextures alias index, for SelectTextureFb / GetFramebufferTextureId
};

// Handles the SoH3D 3DS render pass (P3) needs to record its own draws into the SAME command
// buffer + render pass the Fast3D SDL3-GPU backend has open for the current framebuffer, so the
// OoT3D content interleaves depth-correctly with the N64 geometry. Mirrors SoH3DVkContext. Filled
// by GfxRenderingAPISdl3Gpu::BeginSoH3DPass. (P2: handed out as stubs; the SoH3D model port is P3.)
struct SoH3DGpuContext {
    SDL_GPUDevice* device;
    SDL_GPUCommandBuffer* cmd;
    SDL_GPURenderPass* pass;
    SDL_GPUViewport viewport;
    SDL_Rect scissor;
    uint32_t frameIndex;
    uint32_t framesInFlight;
};

class GfxRenderingAPISdl3Gpu : public GfxRenderingAPI {
  public:
    explicit GfxRenderingAPISdl3Gpu(GfxWindowBackendSDL2* windowBackend);
    ~GfxRenderingAPISdl3Gpu() override;

    // P3 hooks (stubbed in P2): hand the SoH3D pass the device/command-buffer/pass handles it needs
    // to record interleaved OoT3D model draws. Return false in P2 (no open pass handed out yet).
    bool BeginSoH3DPass(SoH3DGpuContext& out);
    bool BeginSoH3DOffscreen(SoH3DGpuContext& out);

    // ---- Unified op model (P3): the SoH3D OoT3D content (CMB models, HUD, RmlUi) appends its draws
    // as ops into the SAME deferred op-list as the N64 Fast3D triangles, replayed in ONE render pass
    // in FinishRender. There is NO separate-pass / live-command-buffer handshake — the legacy
    // BeginSoH3DPass model is gone. soh3d_sdl3gpu.cpp owns its own GPU resources (created via the
    // device handle below) and records draws via these two appenders.
    SDL_GPUDevice* GpuDevice() {
        return mDevice;
    }
    SDL_GPUTextureFormat GpuColorFormat() {
        return mColorFormat;
    }
    SDL_GPUTextureFormat GpuDepthFormat() {
        return mDepthFormat;
    }
    int CurrentFb() {
        return mCurrentFb;
    }
    // The current viewport/scissor (as set by the interpreter for the N64 geometry) converted to the
    // SDL3 GPU top-left convention for the current framebuffer — the SAME conversion DrawTriangles
    // applies — so SoH3D model draws land pixel-aligned with the N64 geometry they interleave with.
    void GetSoH3DViewportScissor(SDL_GPUViewport& vp, SDL_Rect& sc);
    // Append an external draw recorded INSIDE the current framebuffer's render pass (interleaves
    // depth-correctly with N64 geometry — same color+depth target). The callback gets the open
    // command buffer + render pass at replay time.
    void AppendSoH3DInPass(std::function<void(SDL_GPUCommandBuffer*, SDL_GPURenderPass*)> fn);
    // Append an external op that runs its OWN render pass (offscreen shadow/AO depth targets). The
    // main framebuffer pass is ended first (SDL3 GPU passes can't nest); the callback owns
    // SDL_BeginGPURenderPass/SDL_EndGPURenderPass on the supplied command buffer.
    void AppendSoH3DOwnPass(std::function<void(SDL_GPUCommandBuffer*)> fn);

    const char* GetName() override;
    int GetMaxTextureSize() override;
    GfxClipParameters GetClipParameters() override;
    void UnloadShader(ShaderProgram* oldPrg) override;
    void LoadShader(ShaderProgram* newPrg) override;
    void ClearShaderCache() override;
    ShaderProgram* CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) override;
    ShaderProgram* LookupShader(uint64_t shaderId0, uint64_t shaderId1) override;
    void ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) override;
    uint32_t NewTexture() override;
    void SelectTexture(int tile, uint32_t textureId) override;
    void UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) override;
    void SetSamplerParameters(int sampler, bool linearFilter, uint32_t cms, uint32_t cmt) override;
    void SetDepthTestAndMask(bool depthTest, bool zUpd) override;
    void SetZmodeDecal(bool decal) override;
    void SetViewport(int x, int y, int width, int height) override;
    void SetScissor(int x, int y, int width, int height) override;
    void SetUseAlpha(bool useAlpha) override;
    void DrawTriangles(float bufVbo[], size_t bufVboLen, size_t bufVboNumTris) override;
    void Init() override;
    void OnResize() override;
    void StartFrame() override;
    void EndFrame() override;
    void FinishRender() override;
    int CreateFramebuffer() override;
    void UpdateFramebufferParameters(int fbId, uint32_t width, uint32_t height, uint32_t msaaLevel, bool openglInvertY,
                                     bool renderTarget, bool hasDepthBuffer, bool canExtractDepth) override;
    void StartDrawToFramebuffer(int fbId, float noiseScale) override;
    void CopyFramebuffer(int fbDstId, int fbSrcId, int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0,
                         int dstX1, int dstY1) override;
    void ClearFramebuffer(bool color, bool depth) override;
    void ReadFramebufferToCPU(int fbId, uint32_t width, uint32_t height, uint16_t* rgba16Buf) override;
    void ResolveMSAAColorBuffer(int fbIdTarger, int fbIdSrc) override;
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
    GetPixelDepth(int fbId, const std::set<std::pair<float, float>>& coordinates) override;
    void* GetFramebufferTextureId(int fbId) override;
    void SelectTextureFb(int fbId) override;
    void DeleteTexture(uint32_t texId) override;
    void SetTextureFilter(FilteringMode mode) override;
    FilteringMode GetTextureFilter() override;
    void SetSrgbMode() override;
    ImTextureID GetTextureById(int id) override;
    void SetCurrentPrimDepth(float depth) override;

  private:
    // ---- Deferred-command recording ----
    // SDL3 GPU forbids transfer (vertex/texture) uploads inside a render pass, and clears are
    // expressed via load_op at pass-begin (not mid-pass like Vulkan's vkCmdClearAttachments). So the
    // backend records draws/clears/blits into an op list during the frame (with vertex data staged
    // into a CPU buffer), then at EndFrame uploads the whole vertex buffer in ONE copy pass and
    // replays the ops — switching render passes only when the target framebuffer changes.
    // OP_EXT_IN_PASS / OP_EXT_OWN_PASS are the unified-renderer hooks (P3): the SoH3D OoT3D content
    // records its draws as callback ops in the SAME stream as the N64 triangles.
    enum OpKind { OP_DRAW, OP_CLEAR, OP_COPY, OP_EXT_IN_PASS, OP_EXT_OWN_PASS };
    struct Op {
        OpKind kind;
        int fb;     // target fb (DRAW/CLEAR/EXT_IN_PASS) or destination fb (COPY)
        int srcFb;  // COPY source fb
        bool clearColor, clearDepth;
        SDL_Rect srcRect, dstRect; // COPY
        bool nearest;              // COPY filter
        // DRAW
        SDL_GPUGraphicsPipeline* pipeline;
        uint32_t vboOffset; // byte offset into the frame vertex buffer
        uint32_t numVerts;
        uint32_t numSamplers;
        SDL_GPUTextureSamplerBinding samplers[6];
        SDL_GPUViewport viewport;
        SDL_Rect scissor;
        uint8_t ubo[64]; // std140 fragment UBO payload (SgUboData)
        // EXT_IN_PASS: invoked inside fb's render pass. EXT_OWN_PASS: invoked with the main pass ended.
        std::function<void(SDL_GPUCommandBuffer*, SDL_GPURenderPass*)> extIn;
        std::function<void(SDL_GPUCommandBuffer*)> extOwn;
    };

    void CreateDeviceAndClaim();
    SDL_GPUGraphicsPipeline* GetOrCreatePipeline(ShaderProgramSDL3* prg, uint32_t stateBits);
    SDL_GPUSampler* GetOrCreateSampler(bool linear, uint32_t cms, uint32_t cmt);
    SDL_GPUTexture* DummyTexture();
    SDL_GPUSampler* DummySampler();
    void CreateFbResources(FramebufferSDL3& fb, uint32_t width, uint32_t height, bool hasDepth);
    void DestroyFbResources(FramebufferSDL3& fb);
    std::string BuildShaderSource(const struct CCFeatures& cc, bool vertex, ShaderProgramSDL3* prg);
    SDL_GPUShader* CreateShader(const std::vector<uint32_t>& spirv, bool vertex, uint32_t numSamplers,
                                uint32_t numUniformBuffers);
    // Acquire the frame command buffer if not already acquired.
    void EnsureCommandBuffer();
    // Upload the staged vertices to the GPU vertex buffer (one copy pass) and replay the recorded
    // op list into the given command buffer. If presentTex != nullptr, blit fb 0 onto it at the end.
    void ReplayOps(SDL_GPUTexture* presentTex, uint32_t presentW, uint32_t presentH);
    // Submit what is recorded so far, wait for the GPU, then start fresh (for CPU readback paths).
    void FlushAndWait();
    void WriteFbPpm(int fbId, const char* path);
    void MaybeDumpFrame();

    GfxWindowBackendSDL2* mWindowBackend = nullptr;
    SDL_Window* mWindow = nullptr;
    SDL_GPUDevice* mDevice = nullptr;
    SDL_GPUTextureFormat mColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUTextureFormat mDepthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    // Frame command buffer (acquired lazily, submitted at FinishRender / FlushAndWait).
    SDL_GPUCommandBuffer* mCmd = nullptr;
    bool mFrameAcquired = false;

    // Per-combiner shader records keyed by (id0,id1); pipelines cached by (id0,id1,stateBits).
    std::map<std::pair<uint64_t, uint64_t>, ShaderProgramSDL3> mShaderProgramPool;
    std::unordered_map<Sdl3PipelineKey, SDL_GPUGraphicsPipeline*, Sdl3PipelineKeyHash> mPipelineCache;
    std::map<uint32_t, SDL_GPUSampler*> mSamplerCache;

    // Per-frame vertex staging: a host transfer buffer (mapped during the frame) + a device vertex
    // buffer the transfer buffer is uploaded into at EndFrame.
    SDL_GPUTransferBuffer* mVtxTransfer = nullptr;
    SDL_GPUBuffer* mVbo = nullptr;
    uint8_t* mVtxMapped = nullptr;
    uint32_t mVtxCapacity = 0;
    uint32_t mVtxUsed = 0;

    std::vector<Op> mOps;

    SDL_GPUTexture* mDummyTex = nullptr;
    SDL_GPUSampler* mDummySampler = nullptr;

    std::vector<TextureSDL3> mTextures; // indexed by texture id
    std::vector<FramebufferSDL3> mFramebuffers;

    // Current draw state. (mCurrentDepthTest/Mask/ZmodeDecal, mSrgbMode and mCurrentPrimDepth live
    // in the GfxRenderingAPI base class — do not shadow them.)
    ShaderProgramSDL3* mCurrentShaderProgram = nullptr;
    uint32_t mCurrentTextureIds[6] = { 0, 0, 0, 0, 0, 0 };
    uint8_t mCurrentTile = 0;
    bool mCurrentUseAlpha = false;
    float mCurrentNoiseScale = 0.0f;
    uint32_t mFrameCount = 0;
    SDL_GPUViewport mCurrentViewport{};
    SDL_Rect mCurrentScissor{};
    int mCurrentFb = 0;

    FilteringMode mCurrentFilterMode = FILTER_THREE_POINT;
    uint32_t mNextTextureId = 1;
    int mMaxTextureSize = 8192;
};

// Set to the live SDL3-GPU backend in Init() (cleared in the destructor); null when another backend
// is active. The SoH3D SDL3-GPU pass (P3) will use this to find the backend it must record into.
extern GfxRenderingAPISdl3Gpu* g_activeSdl3GpuApi;
} // namespace Fast

#endif // ENABLE_SDL3GPU
