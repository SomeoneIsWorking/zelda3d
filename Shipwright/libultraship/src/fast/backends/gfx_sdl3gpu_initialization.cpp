#include "ship/window/Window.h"
#ifdef ENABLE_SDL3GPU

#include "fast/backends/gfx_sdl3gpu.h"
#include "fast/backends/gfx_sdl.h"
#include "fast/backends/unified_n64_pack.h"
#include "fast/backends/unified_shader.h"
#include "fast/backends/zelda3d_sdl3gpu.h"
#include "fast/interpreter.h"

#include <cstdlib>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

using Fast::GfxRenderingAPISdl3Gpu;
using Fast::Zelda3DHudRenderer;
using Fast::Zelda3DRenderer;

void GfxRenderingAPISdl3Gpu::Init() {
    mWindow = mWindowBackend ? mWindowBackend->GetSdlWindow() : nullptr;
    if (mWindow == nullptr) {
        SPDLOG_ERROR("SDL3 GPU backend: SDL window is null at Init()");
        abort();
    }
    CreateDeviceAndClaim();

    // Per-frame vertex staging buffers (transfer + device vertex buffer). 48 MB matches the Vulkan
    // backend's 32 MB ring with headroom; one frame's worth of geometry uploaded in one copy pass.
    mVtxCapacity = 48u * 1024u * 1024u;
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = mVtxCapacity;
    mVtxTransfer = SDL_CreateGPUTransferBuffer(mDevice, &transferInfo);
    SDL_GPUBufferCreateInfo bufferInfo{};
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bufferInfo.size = mVtxCapacity;
    mVbo = SDL_CreateGPUBuffer(mDevice, &bufferInfo);
    if (mVtxTransfer == nullptr || mVbo == nullptr) {
        SPDLOG_ERROR("SDL3 GPU: vertex buffer alloc failed: {}", SDL_GetError());
        abort();
    }

    // Slot 0 is the main framebuffer; the interpreter sizes it via UpdateFramebufferParameters(0,..)
    // right after Init and every frame.
    mFramebuffers.resize(1);
    mFramebuffers[0].renderTarget = true;
    Fast::g_activeSdl3GpuApi = this;

    // Compile the fixed model shaders while the backend is initializing. The Zelda3D target cannot
    // operate without this renderer, so refusing startup is the correct response to invalid source.
    mSoh3d = std::make_unique<Zelda3DRenderer>();
    if (!mSoh3d->initializeGpuResources()) {
        SPDLOG_ERROR("SDL3 GPU backend: Zelda3D renderer resource initialization failed");
        abort();
    }
    mHud = std::make_unique<Zelda3DHudRenderer>();
    SPDLOG_INFO("SDL3 GPU backend initialized (P2: N64 Fast3D world)");

    // The live unified routes compile variants lazily; this optional startup self-test compiles the
    // complete variant set up front so source-generation regressions fail before the first draw.
    if (getenv("ZELDA3D_UNIFIED_SHADER_SELFTEST") != nullptr) {
        std::string log;
        if (Fast::Unified::SelfTestUnifiedShaderVariants(log)) {
            SPDLOG_INFO("[unified_shader] selftest: all variants compiled OK");
        } else {
            SPDLOG_ERROR("[unified_shader] selftest FAILED:\n{}", log);
        }
        for (uint64_t shaderId : { uint64_t{ 0 }, uint64_t{ 0x01010100 } }) {
            CCFeatures features{};
            gfx_cc_get_features(shaderId, shaderId, &features);
            const UnifiedMaterial material = Fast_PackCCFeaturesToUnifiedMaterial(features);
            SPDLOG_INFO("[unified_n64_pack] shaderId={:#x} -> combMux[0][0]=({},{},{},{}) cycleCount={}", shaderId,
                        material.combMux[0][0][0], material.combMux[0][0][1], material.combMux[0][0][2],
                        material.combMux[0][0][3], material.cycleCount);
        }
    }
}

#endif // ENABLE_SDL3GPU
