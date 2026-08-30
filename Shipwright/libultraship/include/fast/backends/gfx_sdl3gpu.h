#ifdef ENABLE_SDL3GPU
#pragma once

#include "gfx_rendering_api.h"
#include "../interpreter.h"
#include "fast/zelda3d_sg_ubo.h" // Zelda3DSg::SgUbo — the skinned model-draw uniform payload carried by OP_DRAW

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <vector>
#include <map>
#include <array>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <functional>
#include <memory>

namespace Fast {

// Cumulative shader compiles this process. Printed per run by Ship::Context::BeginRun; the delta
// between runs is what says whether the shader cache survives a run boundary.
size_t Sdl3GpuShaderCompileCount();

// Release glslang's process-wide pools. Call ONCE, at engine shutdown, after the last shader compile.
void Sdl3GpuFinalizeShaderCompiler();

// The OoT3D model renderer + HUD, folded into this backend as member subsystems (see
// fast/backends/zelda3d_sdl3gpu.h). Forward-declared here so only gfx_sdl3gpu.cpp + the two zelda3d
// .cpp files need the full definitions.
class Zelda3DRenderer;
class Zelda3DHudRenderer;

// Forward declaration of the SDL window backend so the SDL3-GPU rendering API can pull the
// SDL_Window out of it (to claim it for the GPU device). gfx_sdl2.cpp is the shared window
// manager for GL / Metal / Vulkan / SDL3-GPU on SDL.
class GfxWindowBackendSDL3;

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
    // Render-unification effort (kanban #131), Phase 3 groundwork: the full combiner decode this
    // program was built from. Not used by the existing per-permutation shader-gen path (which
    // already consumed it once at CreateAndLoadNewShader time and only needs numInputs/usedTextures/
    // clamp/opt_fog/opt_grayscale/opt_alpha below) — kept so a future N64->UnifiedVtx converter can
    // know the semantic meaning of each packed-float `attribs` slot without re-decoding the shader
    // id, and so a future N64 unified-pipeline path can build UnifiedMaterial via
    // Fast_PackCCFeaturesToUnifiedMaterial (unified_n64_pack.h) from the SAME decode already done.
    CCFeatures cc{};
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

// Handles the Zelda3D 3DS render pass (P3) needs to record its own draws into the SAME command
// buffer + render pass the Fast3D SDL3-GPU backend has open for the current framebuffer, so the
class GfxRenderingAPISdl3Gpu : public GfxRenderingAPI {
  public:
    explicit GfxRenderingAPISdl3Gpu(GfxWindowBackendSDL3* windowBackend);
    ~GfxRenderingAPISdl3Gpu() override;

    // ---- Unified op model: the Zelda3D OoT3D content (CMB models, HUD, AO composite, RmlUi menu) records
    // its draws into the SAME deferred op-list as the N64 Fast3D triangles, replayed in ONE render pass
    // in FinishRender. Every in-pass draw is a first-class OP_DRAW (AppendZelda3DModelDraw / HudDraw /
    // Fullscreen); only genuine offscreen passes use AppendZelda3DOwnPass. The focused Zelda3D renderer owns its
    // GPU resources (created via the device handle below).
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
    // applies — so Zelda3D model draws land pixel-aligned with the N64 geometry they interleave with.
    void GetZelda3DViewportScissor(SDL_GPUViewport& vp, SDL_Rect& sc);
    // Append a Zelda3D skinned model draw as a FIRST-CLASS OP_DRAW in the unified op-list (no callback
    // indirection): it interleaves with N64 geometry in the same fb pass and replays through the same
    // single fragment-sampler bind path. `ubo` points at a full Zelda3DSg::SgUbo (common + bones). Slot
    // 0 = the model texture, slot 1 = the sun-shadow map (or the dummies when shadow is off),
    // slot 2 = the dual-texture detail mask (nullptr -> dummy; only sampled when the draw's
    // dual-tex flag is set in its UBO). `hasBlendConst`/`blendConst` carry the CMB material's
    // blend_color for pipelines built with a CONSTANT_COLOR blend factor: blend constants are
    // render-pass state rather than pipeline state, so ReplayOps sets them per draw.
    void AppendZelda3DModelDraw(SDL_GPUGraphicsPipeline* pipeline, SDL_GPUBuffer* vbo, uint32_t first, uint32_t count,
                                const void* ubo, SDL_GPUTexture* tex, SDL_GPUSampler* samp, SDL_GPUTexture* tex2,
                                SDL_GPUSampler* samp2, SDL_GPUTexture* tex1, SDL_GPUSampler* samp1,
                                const SDL_GPUViewport& vp, const SDL_Rect& sc, bool hasBlendConst = false,
                                SDL_FColor blendConst = SDL_FColor{ 0.0f, 0.0f, 0.0f, 1.0f });
    // Append one coalesced HUD quad-run as a first-class OP_DRAW into fb 0 (on top of the N64 + model
    // content, in the same pass), through the same single fragment-sampler bind path. `vbo` is the HUD
    // ring vertex buffer; the vertex shader's viewport UBO is built from w/h.
    void AppendZelda3DHudDraw(SDL_GPUGraphicsPipeline* pipeline, SDL_GPUBuffer* vbo, uint32_t first, uint32_t count,
                              SDL_GPUTexture* tex, SDL_GPUSampler* samp, float w, float h);
    // Append a fullscreen-triangle post-process draw (the SSAO composite) as a first-class OP_DRAW into
    // the current fb pass, over the scene, through the same single bind path. No vertex buffer (the
    // vertex shader generates the triangle from gl_VertexIndex); `ubo`/`uboLen` is its fragment UBO and
    // `tex`/`samp` its single sampler. The pipeline carries the multiply blend.
    void AppendZelda3DFullscreen(SDL_GPUGraphicsPipeline* pipeline, const void* ubo, uint32_t uboLen,
                                 SDL_GPUTexture* tex, SDL_GPUSampler* samp, const SDL_GPUViewport& vp,
                                 const SDL_Rect& sc);
    // Append an external op that runs its OWN render pass (offscreen shadow/AO depth targets). The
    // main framebuffer pass is ended first (SDL3 GPU passes can't nest); the callback owns
    // SDL_BeginGPURenderPass/SDL_EndGPURenderPass on the supplied command buffer.
    void AppendZelda3DOwnPass(std::function<void(SDL_GPUCommandBuffer*)> fn);
    // Pixel dimensions of the main framebuffer (fb 0) — the HUD / menu pixel-space coordinate basis.
    void MainFbSize(int& w, int& h);
    // The fb 0 color SDL_GPUTexture — for the RmlUi menu's own offscreen composite pass (it loads
    // this colour, draws the stencil-clipped menu over it, stores it back) without disturbing fb 0's
    // D32_FLOAT depth (which the GetPixelDepth readback reads as raw float, so it must NOT carry a
    // stencil aspect). null if fb 0 has no colour yet.
    SDL_GPUTexture* MainFbColorTexture() {
        return (!mFramebuffers.empty()) ? mFramebuffers[0].color : nullptr;
    }
    // True while a frame is recording (StartFrame ran, FinishRender has not) — HUD/menu ops appended
    // now will replay this frame.
    bool FrameRecording() {
        return mFrameAcquired;
    }

    // The folded-in OoT3D model renderer + PC HUD member subsystems (created in Init, reset in the
    // destructor before the device is destroyed). The Zelda3D_Sg_* / Zelda3D_Hud_* C-ABI shims forward
    // here via Fast::g_activeSdl3GpuApi.
    Zelda3DRenderer* Soh3d() {
        return mSoh3d.get();
    }
    Zelda3DHudRenderer* Hud() {
        return mHud.get();
    }

    // Shared GPU resources the folded-in Zelda3D subsystems draw with, so there is ONE sampler cache
    // and ONE set of dummy (1x1 white tex + sampler) resources across the whole backend rather than
    // the model renderer keeping its own duplicates. GetOrCreateSamplerEx is the general primitive
    // (fully-resolved SDL params + max_lod); the N64 GetOrCreateSampler(linear,cms,cmt) is a thin
    // wrapper over it. CMB bindings carry independent minification, magnification, and mip modes,
    // so the Zelda3D model path resolves those authored enums before entering this cache.
    SDL_GPUSampler* GetOrCreateSamplerEx(SDL_GPUFilter minFilter, SDL_GPUFilter magFilter,
                                         SDL_GPUSamplerMipmapMode mipmapMode, SDL_GPUSamplerAddressMode u,
                                         SDL_GPUSamplerAddressMode v, float maxLod);
    SDL_GPUTexture* DummyTexture();
    SDL_GPUSampler* DummySampler();

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
    void* GetTextureById(int id) override;
    void SetCurrentPrimDepth(float depth) override;

  private:
    // ---- Deferred-command recording ----
    // SDL3 GPU forbids transfer (vertex/texture) uploads inside a render pass, and clears are
    // expressed via load_op at pass-begin (not mid-pass like Vulkan's vkCmdClearAttachments). So the
    // backend records draws/clears/blits into an op list during the frame (with vertex data staged
    // into a CPU buffer), then at EndFrame uploads the whole vertex buffer in ONE copy pass and
    // replays the ops — switching render passes only when the target framebuffer changes.
    // Every in-pass draw — N64 Fast3D, Zelda3D OoT3D skinned models, the PC HUD, and the fullscreen AO
    // composite — is an OP_DRAW (see Op::DrawClass), so the whole frame replays through one render pass
    // and one fragment-sampler bind path. OP_EXT_OWN_PASS remains the hook for genuine OFFSCREEN passes
    // (the shadow / AO depth renders and the RmlUi menu stencil pass) that own their own render pass.
    enum OpKind { OP_DRAW, OP_CLEAR, OP_COPY, OP_EXT_OWN_PASS };
    struct Op {
        OpKind kind;
        int fb;    // target fb (DRAW/CLEAR/EXT_IN_PASS) or destination fb (COPY)
        int srcFb; // COPY source fb
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
        // Blend constants for a pipeline that uses SDL_GPU_BLENDFACTOR_[ONE_MINUS_]CONSTANT_COLOR
        // (3DS CMB water/waterfall materials: dst = GL_CONSTANT_ALPHA). These are RENDER-PASS
        // state, so ReplayOps issues SDL_SetGPUBlendConstants for the ops that need it (tracked
        // against the pass's current value so unrelated draws cost nothing).
        bool useBlendConstants;
        SDL_FColor blendConstants;
        uint8_t ubo[64]; // std140 uniform payload: N64 = fragment SgUboData; HUD = vertex viewport UBO
        // Vertex source. altVbo != null binds that buffer (Zelda3D model's per-model vbo, or the HUD ring
        // vbo) at offset 0; null falls back to the shared frame mVbo at vboOffset (N64). numVerts +
        // firstVertex are the draw range for all three.
        SDL_GPUBuffer* altVbo;
        uint32_t firstVertex;
        // Which uniform-push + sampler shape this OP_DRAW uses. The pipeline, fragment-sampler bind
        // path, viewport/scissor and the draw are shared across all three; only the uniform push and
        // the vertex layout (baked into the pipeline) differ.
        //   DRAW_N64        : one fragment UBO (op.ubo) from mVbo.
        //   DRAW_MODEL      : skinned — the 3-block common+bones push from mSoh3dModelUbos[zelda3dDrawIdx].
        //   DRAW_HUD        : one vertex UBO (op.ubo, the viewport) from altVbo.
        //   DRAW_FULLSCREEN : one fragment UBO (op.ubo, uboLen bytes), NO vertex buffer (the AO composite
        //                     generates a fullscreen triangle from gl_VertexIndex).
        enum DrawClass : uint8_t { DRAW_N64, DRAW_MODEL, DRAW_HUD, DRAW_FULLSCREEN } drawClass;
        uint16_t uboLen;    // fragment-UBO byte length for DRAW_FULLSCREEN (0 = use the class default)
        int zelda3dDrawIdx; // DRAW_MODEL: index into mSoh3dModelUbos for the oversized skinned payload
        std::function<void(SDL_GPUCommandBuffer*)> extOwn; // OP_EXT_OWN_PASS: runs with the main pass ended
    };

    void CreateDeviceAndClaim();
    SDL_GPUGraphicsPipeline* GetOrCreatePipeline(ShaderProgramSDL3* prg, uint32_t stateBits);
    SDL_GPUSampler* GetOrCreateSampler(bool linear, uint32_t cms, uint32_t cmt);
    // Texture-release lifetime helpers: defer a release while a frame is recording (the op-list may
    // still bind the handle), flush deferred releases once that frame's command buffer is submitted.
    void DeferReleaseTexture(SDL_GPUTexture* tex);
    void FlushPendingTexReleases();
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
    void WriteFbDepthPpm(int fbId, const char* path);
    // Zelda3D direct-harness capture: reads fb <fbId> (color texture) into
    // gSoh3dCaptureBuf as RGBA8. Same GPU download pattern as WriteFbPpm
    // but the pixels land in a caller-supplied buffer for in-process
    // side-by-side compositing (no disk round-trip).
    void WriteFbToCaptureBuf(int fbId);
    void MaybeDumpFrame();

    GfxWindowBackendSDL3* mWindowBackend = nullptr;
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

    // Render-unification effort (kanban #131), Phase 3: N64 draws routed through the unified
    // shader when gUnifiedRenderer & 2. Separate shader/pipeline caches from the old per-permutation
    // ones above so gUnifiedRenderer==0 never touches this state. The pipeline SHAPE only depends on
    // the structural variant (texture count/alpha-test/fog/grayscale) + stateBits (depth/blend) —
    // unlike the old path, the combiner mux itself lives in the per-draw UBO now, not the shader
    // text, so no per-(id0,id1) pipeline explosion. Key = variant*16 + stateBits.
    SDL_GPUShader* mUniN64Vert[6] = {};
    SDL_GPUShader* mUniN64Frag[6] = {};
    std::unordered_map<uint32_t, SDL_GPUGraphicsPipeline*> mUniN64PipelineCache;
    SDL_GPUGraphicsPipeline* GetOrCreateUnifiedN64Pipeline(int variant, uint32_t stateBits);
    std::map<uint32_t, SDL_GPUSampler*> mSamplerCache;

    // Per-frame vertex staging: a host transfer buffer (mapped during the frame) + a device vertex
    // buffer the transfer buffer is uploaded into at EndFrame.
    SDL_GPUTransferBuffer* mVtxTransfer = nullptr;
    SDL_GPUBuffer* mVbo = nullptr;
    uint8_t* mVtxMapped = nullptr;
    uint32_t mVtxCapacity = 0;
    uint32_t mVtxUsed = 0;

    std::vector<Op> mOps;
    // Oversized skinned-model uniform payloads (common + bones, ~4.4 KB) for the DRAW_MODEL OP_DRAWs in
    // mOps — kept out of Op so they don't bloat the thousands of N64 ops. Cleared in lockstep with mOps
    // so an op's zelda3dDrawIdx always indexes the live vector.
    std::vector<std::array<uint8_t, sizeof(Zelda3DSg::SgUbo)>> mSoh3dModelUbos;

    // GPU textures whose release was deferred because the in-flight op-list may still bind them;
    // released in FlushPendingTexReleases once the frame's command buffer is submitted.
    std::vector<SDL_GPUTexture*> mPendingTexRelease;

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

    // Folded-in renderer subsystems (unique_ptr so the header only needs forward declarations).
    std::unique_ptr<Zelda3DRenderer> mSoh3d;
    std::unique_ptr<Zelda3DHudRenderer> mHud;
};

// Set to the live SDL3-GPU backend in Init() (cleared in the destructor); null when another backend
// is active. The Zelda3D SDL3-GPU pass (P3) will use this to find the backend it must record into.
extern GfxRenderingAPISdl3Gpu* g_activeSdl3GpuApi;
} // namespace Fast

#endif // ENABLE_SDL3GPU
