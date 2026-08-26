// Zelda3D SDL3GPU device-resource teardown ownership.
#ifdef ENABLE_SDL3GPU

#include "fast/backends/zelda3d_sdl3gpu.h"

bool Fast::Zelda3DRenderer::initializeGpuResources() {
    return ensureResources();
}

void Fast::Zelda3DRenderer::releaseGpuResources(bool (*note)(const void* handle, const char* what)) {
    if (g_device == nullptr) {
        return;
    }

    // Per-model geometry and textures. This is the bulk of it -- the 362 VkImageViews the validation
    // layer reported are these.
    for (auto& kv : g_models) {
        SgModel& model = kv.second;
        if (model.vbo != nullptr && note(model.vbo, "zelda3d model vbo")) {
            SDL_ReleaseGPUBuffer(g_device, model.vbo);
        }
        model.vbo = nullptr;
        for (SDL_GPUTexture* texture : model.textures) {
            // note() returns false for anything already released, which is what makes it safe to walk
            // a list that may contain the backend's shared dummy texture rather than an owned one.
            if (texture != nullptr && note(texture, "zelda3d model texture")) {
                SDL_ReleaseGPUTexture(g_device, texture);
            }
        }
        model.textures.clear();
    }
    g_models.clear();

    for (auto& pipeline : g_pipelines) {
        if (pipeline.second != nullptr && note(pipeline.second, "zelda3d pipeline")) {
            SDL_ReleaseGPUGraphicsPipeline(g_device, pipeline.second);
        }
    }
    g_pipelines.clear();

    for (auto& pipeline : g_uniPipelines) {
        if (pipeline.second != nullptr && note(pipeline.second, "zelda3d unified pipeline")) {
            SDL_ReleaseGPUGraphicsPipeline(g_device, pipeline.second);
        }
    }
    g_uniPipelines.clear();

    SDL_GPUShader* shaders[] = { g_vert, g_frag, g_overlayDepthFrag };
    for (SDL_GPUShader* shader : shaders) {
        if (shader != nullptr && note(shader, "zelda3d shader")) {
            SDL_ReleaseGPUShader(g_device, shader);
        }
    }
    g_vert = g_frag = g_overlayDepthFrag = nullptr;
    for (int i = 0; i < static_cast<int>(Fast::Unified::Variant::kCount); ++i) {
        if (g_uniVert[i] != nullptr && note(g_uniVert[i], "zelda3d unified vertex shader")) {
            SDL_ReleaseGPUShader(g_device, g_uniVert[i]);
        }
        if (g_uniFrag[i] != nullptr && note(g_uniFrag[i], "zelda3d unified fragment shader")) {
            SDL_ReleaseGPUShader(g_device, g_uniFrag[i]);
        }
        g_uniVert[i] = nullptr;
        g_uniFrag[i] = nullptr;
    }

    if (g_overlayDepthPipe != nullptr && note(g_overlayDepthPipe, "zelda3d overlay depth pipeline")) {
        SDL_ReleaseGPUGraphicsPipeline(g_device, g_overlayDepthPipe);
    }
    g_overlayDepthPipe = nullptr;
    g_overlayDepthResReady = false;
}

#endif // ENABLE_SDL3GPU
