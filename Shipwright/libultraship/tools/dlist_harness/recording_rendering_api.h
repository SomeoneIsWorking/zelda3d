#pragma once

#include <cstdint>

#include <imgui.h>
#include <fast/backends/gfx_rendering_api.h>

namespace Zelda3D::DlistHarness {

class RecordingRenderingApi final : public Fast::GfxRenderingAPI {
  public:
    const char* GetName() override;
    int GetMaxTextureSize() override;
    Fast::GfxClipParameters GetClipParameters() override;
    void UnloadShader(Fast::ShaderProgram*) override;
    void LoadShader(Fast::ShaderProgram*) override;
    void ClearShaderCache() override;
    Fast::ShaderProgram* CreateAndLoadNewShader(uint64_t, uint64_t) override;
    Fast::ShaderProgram* LookupShader(uint64_t, uint64_t) override;
    void ShaderGetInfo(Fast::ShaderProgram*, uint8_t* numInputs, bool usedTextures[2]) override;
    uint32_t NewTexture() override;
    void SelectTexture(int tile, uint32_t textureId) override;
    void UploadTexture(const uint8_t* rgba32Buffer, uint32_t width, uint32_t height) override;
    void SetSamplerParameters(int, bool, uint32_t, uint32_t) override;
    void SetDepthTestAndMask(bool, bool) override;
    void SetZmodeDecal(bool) override;
    void SetViewport(int, int, int, int) override;
    void SetScissor(int, int, int, int) override;
    void SetUseAlpha(bool) override;
    void DrawTriangles(float[], size_t, size_t triangleCount) override;
    void Init() override;
    void OnResize() override;
    void StartFrame() override;
    void EndFrame() override;
    void FinishRender() override;
    int CreateFramebuffer() override;
    void UpdateFramebufferParameters(int, uint32_t, uint32_t, uint32_t, bool, bool, bool, bool) override;
    void StartDrawToFramebuffer(int, float) override;
    void CopyFramebuffer(int, int, int, int, int, int, int, int, int, int) override;
    void ClearFramebuffer(bool, bool) override;
    void ReadFramebufferToCPU(int, uint32_t, uint32_t, uint16_t*) override;
    void ResolveMSAAColorBuffer(int, int) override;
    std::unordered_map<std::pair<float, float>, uint16_t, Fast::hash_pair_ff>
    GetPixelDepth(int, const std::set<std::pair<float, float>>&) override;
    void* GetFramebufferTextureId(int) override;
    void SelectTextureFb(int) override;
    void DeleteTexture(uint32_t) override;
    void SetTextureFilter(Fast::FilteringMode) override;
    Fast::FilteringMode GetTextureFilter() override;
    void SetSrgbMode() override;
    ImTextureID GetTextureById(int) override;
    void SetCurrentPrimDepth(float) override;

    [[nodiscard]] uint32_t UploadCount() const;
    [[nodiscard]] uint32_t TriangleDrawCount() const;

  private:
    int mShaderDummy = 0;
    uint32_t mTextureCounter = 1;
    int mFramebufferCounter = 1;
    int mCurrentTile = -1;
    uint32_t mUploadCount = 0;
    uint32_t mTriangleDrawCount = 0;
};

} // namespace Zelda3D::DlistHarness
