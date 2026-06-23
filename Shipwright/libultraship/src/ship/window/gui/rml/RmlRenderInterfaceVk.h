#pragma once

// RmlUi render interface for the Fast3D Vulkan backend. Records the menu's 2D geometry into the
// backend's current command buffer + render pass (obtained via GfxRenderingAPIVulkan::BeginSoH3DPass,
// the same hook soh3d_vk uses), so the RmlUi menu draws on top of the game under Vulkan — the GL3
// render interface's counterpart. Feature set: compiled geometry (VBO+IBO), generated textures
// (font atlas), scissor, and stencil clip-mask (EnableClipMask/RenderToClipMask — required for
// overflow:hidden with border-radius). Layer/transform/filter effects are still no-op stubs.
//
// Clip-mask requires the depth-stencil attachment to include a stencil aspect, which is why
// gfx_vulkan.cpp uses VK_FORMAT_D32_SFLOAT_S8_UINT (see mDepthFormat).
//
// NOTE: the small Vulkan helpers (memory-type pick, buffer/image upload, one-shot transfer) are
// duplicated from soh3d_vk.cpp for now; a shared libultraship Vulkan-util TU is a TODO.

#ifdef ENABLE_VULKAN

#include <RmlUi/Core/RenderInterface.h>
#include <vulkan/vulkan.h>

#include <vector>
#include <unordered_map>

namespace Ship {

class RmlRenderInterfaceVk : public Rml::RenderInterface {
  public:
    RmlRenderInterfaceVk();
    ~RmlRenderInterfaceVk() override;

    // Frame hooks driven by SohRmlUi around Rml::Context::Render(). BeginFrame grabs the backend's
    // current pass/cmd buffer and resets this frame's UBO ring + descriptor pool; returns false if
    // there is no drawable target this frame (then Render() should be skipped).
    bool BeginFrame();
    void EndFrame();

    void Shutdown();

    // --- Rml::RenderInterface ---
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

    void EnableClipMask(bool enable) override;
    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                          Rml::Vector2f translation) override;

  private:
    struct Geometry {
        VkBuffer vbo = VK_NULL_HANDLE;
        VkDeviceMemory vboMem = VK_NULL_HANDLE;
        VkBuffer ibo = VK_NULL_HANDLE;
        VkDeviceMemory iboMem = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    struct Ring {
        VkBuffer ubo = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize capacity = 0;
        VkDeviceSize offset = 0;
        VkDescriptorPool pool = VK_NULL_HANDLE;
    };
    // Resource freed N frames after release, so it is not destroyed while still in flight.
    struct PendingFree {
        uint64_t freeAtFrame = 0;
        Geometry geo;       // either a geometry...
        Texture tex;        // ...or a texture (the unused one is all-null)
    };

    bool EnsureResources();
    uint32_t FindMemType(uint32_t bits, VkMemoryPropertyFlags want) const;
    void MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& buf,
                    VkDeviceMemory& mem, void** mappedOut);
    void DestroyGeometry(Geometry& g);
    void DestroyTexture(Texture& t);
    void ProcessPendingFrees();

    // Per-frame handles captured from the backend in BeginFrame. They are copied out of the
    // SoH3DVkContext (filled by GfxRenderingAPIVulkan::BeginSoH3DPass) in the .cpp, which alone
    // includes gfx_vulkan.h — that header transitively defines libultraship's `union Gfx`, which
    // collides with RmlUi's `namespace Gfx`, so it must not reach SohRmlUi.cpp via this header.
    bool mActive = false;
    bool mReady = false;
    uint64_t mFrameCounter = 0;

    VkDevice mDevice = VK_NULL_HANDLE;
    VkPhysicalDevice mPhys = VK_NULL_HANDLE;
    VkRenderPass mRenderPass = VK_NULL_HANDLE;
    VkQueue mQueue = VK_NULL_HANDLE;
    VkCommandPool mCmdPool = VK_NULL_HANDLE;
    VkCommandBuffer mCmd = VK_NULL_HANDLE;
    VkViewport mViewport{};
    VkRect2D mFullScissor{};
    uint32_t mFrameIndex = 0;
    uint32_t mFramesInFlight = 0;
    VkDescriptorSetLayout mSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout mPipeLayout = VK_NULL_HANDLE;
    VkShaderModule mVs = VK_NULL_HANDLE, mFs = VK_NULL_HANDLE;
    VkPipeline mPipeline = VK_NULL_HANDLE;          // normal draw (stencil test; color writes on)
    VkPipeline mPipelineMaskWrite = VK_NULL_HANDLE; // clip-mask write (color writes off; stencil write on)
    VkSampler mSampler = VK_NULL_HANDLE;
    Texture mWhiteTex{};
    VkDeviceSize mUboStride = 0;
    std::vector<Ring> mRings;

    bool mScissorEnabled = false;
    VkRect2D mScissor{};

    // Clip-mask (stencil) state.
    bool mClipMaskEnabled = false;
    uint8_t mStencilRef = 0; // current stencil reference value for the test pipeline

    std::unordered_map<Rml::CompiledGeometryHandle, Geometry> mGeometries;
    std::unordered_map<Rml::TextureHandle, Texture> mTextures;
    Rml::CompiledGeometryHandle mNextGeometry = 1;
    Rml::TextureHandle mNextTexture = 1;
    std::vector<PendingFree> mPendingFrees;
};

} // namespace Ship

#endif // ENABLE_VULKAN
