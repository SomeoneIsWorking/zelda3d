#include "frontend_presentation.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL.h>

#include "harness_vk.h"
#include "libretro.h"
#include "libretro_frontend.h"
#include "soh_capture_bridge.h"

namespace HarnessFrontend {

namespace {

SDL_Window* g_window = nullptr;
int g_window_width = 1600;
int g_window_height = 480;

std::vector<uint8_t> g_oracle_pixels;
uint32_t g_oracle_width = 0;
uint32_t g_oracle_height = 0;
std::size_t g_oracle_pitch = 0;
bool g_oracle_dirty = false;

std::vector<uint8_t> g_soh_pixels;

} // namespace

bool Headless() {
    const char* value = std::getenv("SOH3D_HARNESS_HEADLESS");
    return value && *value && value[0] != '0';
}

int ResolutionFactor() {
    static int factor = -1;
    if (factor < 0) {
        factor = 2;
        if (const char* value = std::getenv("ZELDA3D_HARNESS_RES_FACTOR")) {
            const int requested = std::atoi(value);
            if (requested >= 1 && requested <= 8) {
                factor = requested;
            }
        }
    }
    return factor;
}

void EnsureWindow() {
    if (g_window || Headless()) {
        return;
    }
    g_window_width = 2 * 400 * ResolutionFactor();
    g_window_height = 240 * ResolutionFactor();
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "harness: SDL_InitSubSystem(VIDEO) failed: %s\n", SDL_GetError());
        return;
    }
    g_window =
        SDL_CreateWindow("SoH3D harness — Azahar | SoH3D", g_window_width, g_window_height, SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        std::fprintf(stderr, "harness: SDL_CreateWindow failed: %s\n", SDL_GetError());
    }
}

void RequestSohCapture(bool sohBooted) {
    if (!sohBooted) {
        return;
    }
    constexpr std::size_t kCapacity = static_cast<std::size_t>(2400) * 2400 * 4;
    if (g_soh_pixels.size() < kCapacity) {
        g_soh_pixels.resize(kCapacity);
    }
    gSoh3dCaptureBuf = g_soh_pixels.data();
    gSoh3dCaptureCap = static_cast<uint32_t>(g_soh_pixels.size());
    gSoh3dCapturePending = 1;
}

void PresentSideBySide() {
    if (!g_window) {
        return;
    }
    SDL_Surface* destination = SDL_GetWindowSurface(g_window);
    if (!destination) {
        return;
    }
    const int halfWidth = destination->w / 2;
    SDL_FillSurfaceRect(destination, nullptr, 0);

    if (g_oracle_width && g_oracle_height && !g_oracle_pixels.empty()) {
        SDL_Surface* source =
            SDL_CreateSurfaceFrom(static_cast<int>(g_oracle_width), static_cast<int>(g_oracle_height),
                                  SDL_PIXELFORMAT_XRGB8888, g_oracle_pixels.data(), static_cast<int>(g_oracle_pitch));
        if (source) {
            SDL_Rect destinationRect{ 0, 0, halfWidth, destination->h };
            SDL_BlitSurfaceScaled(source, nullptr, destination, &destinationRect, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(source);
        }
    }

    // The capture buffer stores RGBA bytes. ABGR8888 is SDL's matching
    // packed format on little-endian hosts.
    if (gSoh3dCaptureW && gSoh3dCaptureH && !g_soh_pixels.empty()) {
        const int width = static_cast<int>(gSoh3dCaptureW);
        const int height = static_cast<int>(gSoh3dCaptureH);
        SDL_Surface* source =
            SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_ABGR8888, g_soh_pixels.data(), width * 4);
        if (source) {
            SDL_Rect destinationRect{ halfWidth, 0, destination->w - halfWidth, destination->h };
            SDL_BlitSurfaceScaled(source, nullptr, destination, &destinationRect, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(source);
        }
    }
    SDL_UpdateWindowSurface(g_window);
    g_oracle_dirty = false;
}

void PumpEventsAndPresent() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            std::fprintf(stderr, "harness: window closed, shutting down\n");
            RequestQuit();
        }
    }
    PresentSideBySide();
    SDL_Delay(16);
}

void SubmitOracleFrame(const void* data, unsigned width, unsigned height, std::size_t pitch) {
    if (!width || !height) {
        return;
    }
    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
        std::size_t readbackPitch = 0;
        if (HarnessVk::Readback(g_oracle_pixels, width, height, readbackPitch)) {
            g_oracle_width = width;
            g_oracle_height = height;
            g_oracle_pitch = readbackPitch;
            g_oracle_dirty = true;
        }
        return;
    }
    if (!data) {
        return;
    }

    const std::size_t required = pitch * height;
    if (g_oracle_pixels.size() < required) {
        g_oracle_pixels.resize(required);
    }
    std::memcpy(g_oracle_pixels.data(), data, required);
    g_oracle_width = width;
    g_oracle_height = height;
    g_oracle_pitch = pitch;
    g_oracle_dirty = true;
}

const std::vector<uint8_t>& OraclePixels() {
    return g_oracle_pixels;
}

uint32_t OracleWidth() {
    return g_oracle_width;
}

uint32_t OracleHeight() {
    return g_oracle_height;
}

std::size_t OraclePitch() {
    return g_oracle_pitch;
}

bool OracleDirty() {
    return g_oracle_dirty;
}

const std::vector<uint8_t>& SohPixels() {
    return g_soh_pixels;
}

} // namespace HarnessFrontend
