#include "recording_rendering_api.h"

#include <cstdio>

namespace Zelda3D::DlistHarness {

const char* RecordingRenderingApi::GetName() {
    return "recording";
}

int RecordingRenderingApi::GetMaxTextureSize() {
    return 16384;
}

Fast::GfxClipParameters RecordingRenderingApi::GetClipParameters() {
    return { false, false };
}

void RecordingRenderingApi::UnloadShader(Fast::ShaderProgram*) {
}

void RecordingRenderingApi::LoadShader(Fast::ShaderProgram*) {
}

void RecordingRenderingApi::ClearShaderCache() {
}

Fast::ShaderProgram* RecordingRenderingApi::CreateAndLoadNewShader(uint64_t, uint64_t) {
    return reinterpret_cast<Fast::ShaderProgram*>(&mShaderDummy);
}

Fast::ShaderProgram* RecordingRenderingApi::LookupShader(uint64_t, uint64_t) {
    return nullptr;
}

void RecordingRenderingApi::ShaderGetInfo(Fast::ShaderProgram*, uint8_t* numInputs, bool usedTextures[2]) {
    *numInputs = 1;
    usedTextures[0] = true;
    usedTextures[1] = false;
}

uint32_t RecordingRenderingApi::NewTexture() {
    return mTextureCounter++;
}

void RecordingRenderingApi::SelectTexture(int tile, uint32_t) {
    mCurrentTile = tile;
}

void RecordingRenderingApi::UploadTexture(const uint8_t* rgba32Buffer, uint32_t width, uint32_t height) {
    ++mUploadCount;
    std::printf("[HARNESS upload] #%u tile=%d %ux%u (%u px) first=%d,%d,%d,%d\n", mUploadCount, mCurrentTile, width,
                height, width * height, rgba32Buffer[0], rgba32Buffer[1], rgba32Buffer[2], rgba32Buffer[3]);
    std::fflush(stdout);
}

void RecordingRenderingApi::SetSamplerParameters(int, bool, uint32_t, uint32_t) {
}

void RecordingRenderingApi::SetDepthTestAndMask(bool, bool) {
}

void RecordingRenderingApi::SetZmodeDecal(bool) {
}

void RecordingRenderingApi::SetViewport(int, int, int, int) {
}

void RecordingRenderingApi::SetScissor(int, int, int, int) {
}

void RecordingRenderingApi::SetUseAlpha(bool) {
}

void RecordingRenderingApi::DrawTriangles(float[], size_t, size_t triangleCount) {
    mTriangleDrawCount += static_cast<uint32_t>(triangleCount);
}

void RecordingRenderingApi::Init() {
}

void RecordingRenderingApi::OnResize() {
}

void RecordingRenderingApi::StartFrame() {
}

void RecordingRenderingApi::EndFrame() {
}

void RecordingRenderingApi::FinishRender() {
}

int RecordingRenderingApi::CreateFramebuffer() {
    return mFramebufferCounter++;
}

void RecordingRenderingApi::UpdateFramebufferParameters(int, uint32_t, uint32_t, uint32_t, bool, bool, bool, bool) {
}

void RecordingRenderingApi::StartDrawToFramebuffer(int, float) {
}

void RecordingRenderingApi::CopyFramebuffer(int, int, int, int, int, int, int, int, int, int) {
}

void RecordingRenderingApi::ClearFramebuffer(bool, bool) {
}

void RecordingRenderingApi::ReadFramebufferToCPU(int, uint32_t, uint32_t, uint16_t*) {
}

void RecordingRenderingApi::ResolveMSAAColorBuffer(int, int) {
}

std::unordered_map<std::pair<float, float>, uint16_t, Fast::hash_pair_ff>
RecordingRenderingApi::GetPixelDepth(int, const std::set<std::pair<float, float>>&) {
    return {};
}

void* RecordingRenderingApi::GetFramebufferTextureId(int) {
    return nullptr;
}

void RecordingRenderingApi::SelectTextureFb(int) {
}

void RecordingRenderingApi::DeleteTexture(uint32_t) {
}

void RecordingRenderingApi::SetTextureFilter(Fast::FilteringMode) {
}

Fast::FilteringMode RecordingRenderingApi::GetTextureFilter() {
    return Fast::FILTER_NONE;
}

void RecordingRenderingApi::SetSrgbMode() {
}

ImTextureID RecordingRenderingApi::GetTextureById(int) {
    return static_cast<ImTextureID>(0);
}

void RecordingRenderingApi::SetCurrentPrimDepth(float) {
}

uint32_t RecordingRenderingApi::UploadCount() const {
    return mUploadCount;
}

uint32_t RecordingRenderingApi::TriangleDrawCount() const {
    return mTriangleDrawCount;
}

} // namespace Zelda3D::DlistHarness
