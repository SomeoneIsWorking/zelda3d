#include "ppm_output.h"

#include <cstdio>
#include <filesystem>
#include <vector>

#include <fast/backends/gfx_rendering_api.h>

namespace Zelda3D::DlistHarness {
namespace {

bool WritePpm(const std::string& path, const uint8_t* pixels, uint32_t width, uint32_t height) {
    const std::filesystem::path outputPath(path);
    if (!outputPath.parent_path().empty()) {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    FILE* output = std::fopen(path.c_str(), "wb");
    if (output == nullptr) {
        std::fprintf(stderr, "[HARNESS] cannot open %s for write\n", path.c_str());
        return false;
    }
    std::fprintf(output, "P6\n%u %u\n255\n", width, height);
    std::fwrite(pixels, 1, static_cast<size_t>(width) * height * 3, output);
    std::fclose(output);
    return true;
}

} // namespace

bool CaptureFramebufferToPpm(Fast::GfxRenderingAPI& renderingApi, const std::string& path, uint32_t width,
                             uint32_t height) {
    constexpr int gameFramebuffer = 1;
    std::vector<uint16_t> rgba16(static_cast<size_t>(width) * height);
    renderingApi.ReadFramebufferToCPU(gameFramebuffer, width, height, rgba16.data());
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 3);
    for (size_t index = 0; index < rgba16.size(); ++index) {
        const uint16_t packed = rgba16[index];
        pixels[index * 3] = static_cast<uint8_t>(((packed >> 11) & 0x1F) * 255 / 31);
        pixels[index * 3 + 1] = static_cast<uint8_t>(((packed >> 6) & 0x1F) * 255 / 31);
        pixels[index * 3 + 2] = static_cast<uint8_t>(((packed >> 1) & 0x1F) * 255 / 31);
    }

    size_t nonBlackPixels = 0;
    for (size_t offset = 0; offset < pixels.size(); offset += 3) {
        if (pixels[offset] != 0 || pixels[offset + 1] != 0 || pixels[offset + 2] != 0) {
            ++nonBlackPixels;
        }
    }
    if (!WritePpm(path, pixels.data(), width, height)) {
        return false;
    }
    std::printf("[HARNESS] wrote %s (%ux%u, %zu/%u non-black px)\n", path.c_str(), width, height, nonBlackPixels,
                width * height);
    return true;
}

} // namespace Zelda3D::DlistHarness
