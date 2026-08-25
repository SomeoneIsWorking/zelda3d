#include "libretro_callbacks.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "frontend_presentation.h"
#include "harness_vk.h"
#include "libretro.h"
#include "libretro_frontend.h"
#include "libretro_vulkan.h"
#include "texpack_setup.h"

namespace HarnessFrontend {

namespace {

const bool g_use_vulkan = [] {
    const char* value = std::getenv("SOH3D_HARNESS_SW");
    return !(value && *value && value[0] != '0');
}();

retro_hw_render_callback g_hardware_render{};
const retro_hw_render_context_negotiation_interface_vulkan* g_vulkan_negotiation = nullptr;
bool g_vulkan_ready = false;

void CoreLog(retro_log_level level, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    std::fprintf(stderr, "[core:%d] ", static_cast<int>(level));
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
}

} // namespace

bool EnvironmentCallback(unsigned command, void* data) {
    switch (command) {
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *static_cast<bool*>(data) = true;
            return true;

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            return true;

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            auto* callback = static_cast<retro_log_callback*>(data);
            callback->log = &CoreLog;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            *static_cast<const char**>(data) = SystemDirectory().c_str();
            return true;

        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *static_cast<const char**>(data) = SaveDirectory().c_str();
            return true;

        case RETRO_ENVIRONMENT_SET_HW_RENDER: {
            auto* callback = static_cast<retro_hw_render_callback*>(data);
            if (!g_use_vulkan || callback->context_type != RETRO_HW_CONTEXT_VULKAN) {
                return false;
            }
            g_hardware_render = *callback;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
            g_vulkan_negotiation = static_cast<const retro_hw_render_context_negotiation_interface_vulkan*>(data);
            return true;

        case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE: {
            const retro_hw_render_interface_vulkan* interface = HarnessVk::Interface();
            if (!interface) {
                return false;
            }
            *static_cast<const void**>(data) = interface;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            auto* variable = static_cast<retro_variable*>(data);
            if (variable->key && std::strcmp(variable->key, "citra_graphics_api") == 0) {
                variable->value = g_use_vulkan ? "Vulkan" : "Software";
                return true;
            }
            if (variable->key && std::strcmp(variable->key, "citra_resolution_factor") == 0) {
                static char resolution[4] = { 0 };
                if (!resolution[0]) {
                    std::snprintf(resolution, sizeof(resolution), "%d", ResolutionFactor());
                }
                variable->value = resolution;
                return true;
            }
            if (variable->key && std::strcmp(variable->key, "citra_custom_textures") == 0) {
                variable->value = TexPackEnabled() ? "enabled" : "disabled";
                return true;
            }
            if (variable->key && std::strcmp(variable->key, "citra_layout_option") == 0) {
                variable->value = "single_screen";
                return true;
            }
            variable->value = nullptr;
            return false;
        }

        default:
            return false;
    }
}

void VideoRefresh(const void* data, unsigned width, unsigned height, std::size_t pitch) {
    SubmitOracleFrame(data, width, height, pitch);
}

void AudioSample(int16_t, int16_t) {}

std::size_t AudioSampleBatch(const int16_t*, std::size_t frames) {
    return frames;
}

bool InitializeOracleVideo() {
    if (!g_use_vulkan) {
        return true;
    }
    if (!g_vulkan_negotiation) {
        std::fprintf(stderr, "soh3d_harness: core never set the Vulkan negotiation interface\n");
        return false;
    }
    const bool initialized = HarnessVk::Init(g_vulkan_negotiation);
    if (initialized && g_hardware_render.context_reset) {
        g_hardware_render.context_reset();
        g_vulkan_ready = true;
    }
    if (!g_vulkan_ready) {
        std::fprintf(stderr, "soh3d_harness: Vulkan HW render bring-up failed\n");
    }
    return g_vulkan_ready;
}

} // namespace HarnessFrontend
