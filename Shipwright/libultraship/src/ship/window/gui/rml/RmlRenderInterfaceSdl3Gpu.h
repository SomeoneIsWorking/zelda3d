#pragma once

// RmlUi render interface for the Fast3D SDL3 GPU backend, on the UNIFIED op model. The RmlUi menu's
// 2D geometry is COLLECTED during Rml::Context::Render() (between BeginFrame/EndFrame) and appended
// as ONE op into the SDL3 GPU backend's deferred op-list, targeting framebuffer 0, so the menu
// replays on top of the game (N64 + OoT3D models + HUD) in the same single render pass. There is no
// live-command-buffer handshake (the Vulkan interface's model is gone).
//
// Feature set (M3 + M3 polish): compiled geometry (VBO+IBO), generated/loaded textures, scissor
// rect, and the STENCIL CLIP-MASK (EnableClipMask / RenderToClipMask — overflow:hidden +
// border-radius now clip correctly). The menu's draw op runs as its OWN offscreen pass that loads
// fb 0's colour and binds a PRIVATE D24S8/D32S8 depth-stencil target (cleared to 0 at pass begin),
// so fb 0's D32_FLOAT depth is untouched (GetPixelDepth still reads it as raw float). Because SDL3
// GPU cannot clear stencil mid-pass (no vkCmdClearAttachments), each Set/Intersect uses a
// monotonically-INCREMENTING stencil ref instead of a per-region clear (normal draws test EQUAL
// against the live ref) — equivalent to the Vulkan path minus the clear.
// LIMITATIONS still deferred: the layer stack / opacity filters (PushLayer/PopLayer/CompositeLayers
// render straight to the menu pass, so filter:opacity()/box-shadow are no-ops) and SetInverse
// clip-mask (rare; treated as no clip).

#ifdef ENABLE_SDL3GPU

#include <RmlUi/Core/RenderInterface.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Ship {

class RmlRenderInterfaceSdl3Gpu : public Rml::RenderInterface {
  public:
    RmlRenderInterfaceSdl3Gpu();
    ~RmlRenderInterfaceSdl3Gpu() override;

    // Driven by SohRmlUi around Rml::Context::Render(). BeginFrame returns false if there is no
    // recording frame (then Render() is skipped); EndFrame appends the collected menu draw op.
    bool BeginFrame();
    void EndFrame();
    void Shutdown();
    void SetViewport(int w, int h);

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

    // Stencil clip-mask (overflow:hidden + border-radius). See header note for the deferred model.
    void EnableClipMask(bool enable) override;
    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                          Rml::Vector2f translation) override;
    // Layers / opacity filters: still deferred (see header note). No-op so content renders straight.
    Rml::LayerHandle PushLayer() override;
    void CompositeLayers(Rml::LayerHandle, Rml::LayerHandle, Rml::BlendMode,
                         Rml::Span<const Rml::CompiledFilterHandle>) override {
    }
    void PopLayer() override {
    }
    Rml::CompiledFilterHandle CompileFilter(const Rml::String&, const Rml::Dictionary&) override {
        return 0;
    }
    void ReleaseFilter(Rml::CompiledFilterHandle) override {
    }

  private:
    struct Geometry {
        SDL_GPUBuffer* vbo = nullptr;
        SDL_GPUBuffer* ibo = nullptr;
        uint32_t indexCount = 0;
    };
    struct Cmd {
        SDL_GPUBuffer* vbo;
        SDL_GPUBuffer* ibo;
        uint32_t indexCount;
        SDL_GPUTexture* tex;
        float translate[2];
        bool scissorOn;
        SDL_Rect scissor;
        bool maskWrite;     // true = write the stencil clip-mask (no colour); false = normal draw
        uint8_t stencilRef; // mask-write: value painted; normal draw: value tested (EQUAL)
    };
    struct PendingFree {
        uint64_t freeAtFrame;
        Geometry geo;
        SDL_GPUTexture* tex;
    };

    bool EnsureResources();
    bool EnsureStencilTarget(int w, int h);
    SDL_GPUTexture* UploadTexture(const void* rgba, int w, int h);
    void ProcessPendingFrees();

    SDL_GPUDevice* mDevice = nullptr;
    bool mReady = false;
    bool mActive = false;
    uint64_t mFrameCounter = 0;
    int mViewportW = 1, mViewportH = 1;

    SDL_GPUShader* mVs = nullptr;
    SDL_GPUShader* mFs = nullptr;
    SDL_GPUGraphicsPipeline* mPipeline = nullptr;     // normal draw: stencil test EQUAL, no stencil write
    SDL_GPUGraphicsPipeline* mPipelineMask = nullptr; // mask write: stencil ALWAYS+REPLACE, no colour
    SDL_GPUSampler* mSampler = nullptr;
    SDL_GPUTexture* mWhiteTex = nullptr;

    // Private depth-stencil target for the menu's own composite pass (D24S8/D32S8; depth unused).
    SDL_GPUTexture* mStencilTarget = nullptr;
    SDL_GPUTextureFormat mStencilFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    int mStencilW = 0, mStencilH = 0;

    bool mScissorEnabled = false;
    SDL_Rect mScissor{};

    // Stencil clip-mask state (per Render() frame). mStencilCounter increments per Set/Intersect so
    // no mid-pass stencil clear is needed; mStencilRef is the value normal draws test EQUAL against.
    bool mClipMaskEnabled = false;
    uint8_t mStencilRef = 0;
    uint8_t mStencilCounter = 0;

    std::unordered_map<Rml::CompiledGeometryHandle, Geometry> mGeometries;
    std::unordered_map<Rml::TextureHandle, SDL_GPUTexture*> mTextures;
    Rml::CompiledGeometryHandle mNextGeometry = 1;
    Rml::TextureHandle mNextTexture = 1;
    Rml::LayerHandle mNextLayer = 1;
    std::vector<Cmd> mCmds;
    std::vector<PendingFree> mPendingFrees;
};

} // namespace Ship

#endif // ENABLE_SDL3GPU
