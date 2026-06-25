#pragma once

// RmlUi render interface for the Fast3D SDL3 GPU backend, on the UNIFIED op model. The RmlUi menu's
// 2D geometry is COLLECTED during Rml::Context::Render() (between BeginFrame/EndFrame) and appended
// as ONE op into the SDL3 GPU backend's deferred op-list, targeting framebuffer 0, so the menu
// replays on top of the game (N64 + OoT3D models + HUD) in the same single render pass. There is no
// live-command-buffer handshake (the Vulkan interface's model is gone).
//
// Feature set (M3): compiled geometry (VBO+IBO), generated/loaded textures, scissor rect.
// LIMITATIONS vs the Vulkan interface (deferred): stencil clip-mask (EnableClipMask/RenderToClipMask
// are no-ops — overflow:hidden + border-radius will not clip) and the layer stack / opacity filters
// (PushLayer/PopLayer/CompositeLayers render straight to fb 0, so filter:opacity()/box-shadow are
// no-ops). The SDL3 GPU backend's depth target is D32_FLOAT (no stencil), so a stencil clip-mask
// needs a backend depth-format change first.

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

    // Clip-mask + layers: deferred (see header note). No-op so content still renders to fb 0.
    void EnableClipMask(bool) override {
    }
    void RenderToClipMask(Rml::ClipMaskOperation, Rml::CompiledGeometryHandle, Rml::Vector2f) override {
    }
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
    };
    struct PendingFree {
        uint64_t freeAtFrame;
        Geometry geo;
        SDL_GPUTexture* tex;
    };

    bool EnsureResources();
    SDL_GPUTexture* UploadTexture(const void* rgba, int w, int h);
    void ProcessPendingFrees();

    SDL_GPUDevice* mDevice = nullptr;
    bool mReady = false;
    bool mActive = false;
    uint64_t mFrameCounter = 0;
    int mViewportW = 1, mViewportH = 1;

    SDL_GPUShader* mVs = nullptr;
    SDL_GPUShader* mFs = nullptr;
    SDL_GPUGraphicsPipeline* mPipeline = nullptr;
    SDL_GPUSampler* mSampler = nullptr;
    SDL_GPUTexture* mWhiteTex = nullptr;

    bool mScissorEnabled = false;
    SDL_Rect mScissor{};

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
