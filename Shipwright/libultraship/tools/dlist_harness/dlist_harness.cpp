// Zelda3D display-list harness composition entry point.

#include <cstdio>
#include <memory>

#include <SDL3/SDL.h>
#include <fast/backends/gfx_sdl.h>
#include <fast/backends/gfx_sdl3gpu.h>
#include <fast/debug/GfxDebugger.h>
#include <fast/interpreter.h>
#include <ship/Context.h>

#include "dlist_fixture.h"
#include "generic_dlist_fixture.h"
#include "harness_options.h"
#include "headless_window_backend.h"
#include "interpreter_instance.h"
#include "ppm_output.h"
#include "recording_rendering_api.h"
#include "sdl3gpu_headless_environment.h"
#include "zelda3d_dlist_fixture.h"

namespace Zelda3D::DlistHarness {
namespace {

int RunHarness(const HarnessOptions& options) {
    Gfx* compiledModel = nullptr;
    if (!options.zelda3dMode) {
        compiledModel = SelectCompiledModel(options.modelName);
        if (compiledModel == nullptr) {
            std::fprintf(stderr,
                         "[HARNESS] unknown/unbuilt model '%s' (have: kibako/pot/gs if their .c was generated)\n",
                         options.modelName.c_str());
            return 2;
        }
    }

    auto* context = Ship::Context::CreateUninitializedInstance("zelda3d_harness", "zelda3d_harness", "");
    context->InitLogging();
    context->InitConfiguration();
    context->InitConsoleVariables();

    auto recordingApi = std::make_unique<RecordingRenderingApi>();
    std::unique_ptr<HeadlessWindowBackend> recordingWindow;
    std::unique_ptr<Fast::GfxWindowBackendSDL3> gpuWindow;
    std::unique_ptr<Fast::GfxRenderingAPISdl3Gpu> gpuApi;
    Fast::GfxWindowBackend* windowApi = nullptr;
    Fast::GfxRenderingAPI* renderingApi = nullptr;
    if (options.gpuMode) {
        ConfigureHeadlessSdl3GpuEnvironment();
        gpuWindow = std::make_unique<Fast::GfxWindowBackendSDL3>();
        gpuApi = std::make_unique<Fast::GfxRenderingAPISdl3Gpu>(gpuWindow.get());
        windowApi = gpuWindow.get();
        renderingApi = gpuApi.get();
    } else {
        recordingWindow = std::make_unique<HeadlessWindowBackend>(options.width, options.height);
        windowApi = recordingWindow.get();
        renderingApi = recordingApi.get();
    }
    auto interpreter = std::make_shared<Fast::Interpreter>();
    Fast::GfxSetInstance(interpreter);
    interpreter->SetGfxDebugger(std::make_shared<Fast::GfxDebugger>());
    interpreter->Init(windowApi, renderingApi, "zelda3d_harness", false, options.width, options.height, 0, 0);

    DlistFixture fixture;
    fixture.viewPlane = options.viewPlane;
    if (options.zelda3dMode) {
        if (!BuildZelda3DDlistFixture(fixture, options.zarPath, 0, options.rotationX, options.rotationY,
                                      options.rotationZ)) {
            return 2;
        }
    } else {
        if (!BuildGenericDlistFixture(fixture, compiledModel)) {
            return 2;
        }
    }

    const std::string& subject = options.zelda3dMode ? options.zarPath : options.modelName;
    std::printf("[HARNESS] running '%s' dlist through LUS interpreter (%s, %ux%u)...\n", subject.c_str(),
                options.gpuMode ? "SDL3GPU" : "recording", options.width, options.height);
    std::fflush(stdout);
    interpreter->StartFrame();
    interpreter->Run(fixture.commands.data(), fixture.matrixReplacements);
    interpreter->EndFrame();

    if (options.gpuMode) {
        CaptureFramebufferToPpm(*renderingApi, options.outputPath, options.width, options.height);
    } else {
        std::printf("[HARNESS] done: %u UploadTexture call(s), %u triangle(s) drawn.\n", recordingApi->UploadCount(),
                    recordingApi->TriangleDrawCount());
    }
    std::fflush(stdout);
    return 0;
}

} // namespace
} // namespace Zelda3D::DlistHarness

int main(int argc, char** argv) {
    return Zelda3D::DlistHarness::RunHarness(Zelda3D::DlistHarness::ParseHarnessOptions(argc, argv));
}
