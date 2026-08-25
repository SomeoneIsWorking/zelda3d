#include "generic_dlist_fixture.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <sys/mman.h>

#include <fast/interpreter.h>

namespace Zelda3D::DlistHarness {
namespace {

constexpr uint8_t SetTextureImageOpcode = 0xFD;
constexpr uint8_t EndDisplayListOpcode = 0xDF;
constexpr uint8_t LoadBlockWideOpcode = 0x47;
constexpr uint8_t LoadBlockOpcode = 0xF3;

size_t TextureByteCount(uint32_t textureSize, uint32_t texelCount) {
    if (textureSize == 0) {
        return (texelCount + 1) / 2;
    }
    if (textureSize == 1) {
        return texelCount;
    }
    if (textureSize == 2) {
        return texelCount * 2;
    }
    return texelCount * 4;
}

uint32_t FindTextureTexelCount(const std::vector<Gfx>& model, size_t textureCommandIndex) {
    for (size_t index = textureCommandIndex + 1; index < model.size(); ++index) {
        const uint8_t opcode = static_cast<uint8_t>(model[index].words.w0 >> 24);
        if (opcode == SetTextureImageOpcode) {
            break;
        }
        if (opcode == LoadBlockWideOpcode) {
            return static_cast<uint32_t>(model[index].words.w1 & 0xFFFFFFFF) + 1;
        }
        if (opcode == LoadBlockOpcode) {
            return (static_cast<uint32_t>(model[index].words.w1 >> 12) & 0xFFF) + 1;
        }
    }
    return 0;
}

void CopyModelDisplayList(DlistFixture& fixture, Gfx* source) {
    for (size_t index = 0;; ++index) {
        fixture.model.push_back(source[index]);
        if (static_cast<uint8_t>(source[index].words.w0 >> 24) == EndDisplayListOpcode) {
            return;
        }
    }
}

bool RelocateLowTexturePointers(DlistFixture& fixture) {
    for (size_t index = 0; index + 1 < fixture.model.size(); ++index) {
        if (static_cast<uint8_t>(fixture.model[index].words.w0 >> 24) != SetTextureImageOpcode) {
            continue;
        }
        const uint32_t textureSize = (fixture.model[index].words.w0 >> 19) & 0x3;
        const uint32_t texelCount = FindTextureTexelCount(fixture.model, index);
        if (texelCount == 0) {
            std::fprintf(stderr, "[HARNESS] G_SETTIMG[%zu] has no following load command\n", index);
            return false;
        }
        const size_t byteCount = TextureByteCount(textureSize, texelCount);
        const auto* original = reinterpret_cast<const uint8_t*>(fixture.model[index].words.w1);
        if (original == nullptr) {
            std::fprintf(stderr, "[HARNESS] G_SETTIMG[%zu] has a null texture pointer\n", index);
            return false;
        }
        void* relocated = mmap(nullptr, byteCount, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (relocated == MAP_FAILED) {
            std::fprintf(stderr, "[HARNESS] could not relocate G_SETTIMG[%zu] (%zu bytes)\n", index, byteCount);
            return false;
        }
        std::memcpy(relocated, original, byteCount);
        std::printf("[HARNESS] relocated G_SETTIMG[%zu] tex %p -> %p (%zu bytes, siz=%u)\n", index,
                    static_cast<const void*>(original), relocated, byteCount, textureSize);
        fixture.model[index].words.w1 = reinterpret_cast<uintptr_t>(relocated);
    }
    return true;
}

void MeasureModelBounds(const std::vector<Gfx>& model, float minPosition[3], float maxPosition[3], size_t& count) {
    for (const Gfx& command : model) {
        if (static_cast<uint8_t>(command.words.w0 >> 24) != G_VTX) {
            continue;
        }
        const uint32_t vertexCount = (command.words.w0 >> 12) & 0xFF;
        const auto* vertices = reinterpret_cast<const uint8_t*>(command.words.w1);
        if (vertices == nullptr) {
            continue;
        }
        for (uint32_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
            const auto* position = reinterpret_cast<const int16_t*>(vertices + static_cast<size_t>(vertexIndex) * 16);
            for (int component = 0; component < 3; ++component) {
                const float value = static_cast<float>(position[component]);
                minPosition[component] = std::min(minPosition[component], value);
                maxPosition[component] = std::max(maxPosition[component], value);
            }
            ++count;
        }
    }
}

void ConfigureViewport(DlistFixture& fixture) {
    fixture.viewport.vp.vscale[0] = (SCREEN_WIDTH / 2) * 4;
    fixture.viewport.vp.vscale[1] = (SCREEN_HEIGHT / 2) * 4;
    fixture.viewport.vp.vscale[2] = G_MAXZ;
    fixture.viewport.vp.vscale[3] = 0;
    fixture.viewport.vp.vtrans[0] = (SCREEN_WIDTH / 2) * 4;
    fixture.viewport.vp.vtrans[1] = (SCREEN_HEIGHT / 2) * 4;
    fixture.viewport.vp.vtrans[2] = 0;
    fixture.viewport.vp.vtrans[3] = 0;
}

} // namespace

bool BuildGenericDlistFixture(DlistFixture& fixture, Gfx* modelDisplayList) {
    CopyModelDisplayList(fixture, modelDisplayList);
    if (!RelocateLowTexturePointers(fixture)) {
        return false;
    }

    float minPosition[3] = { 1e30F, 1e30F, 1e30F };
    float maxPosition[3] = { -1e30F, -1e30F, -1e30F };
    size_t vertexCount = 0;
    MeasureModelBounds(fixture.model, minPosition, maxPosition, vertexCount);

    float center[3] = { 0.0F, 0.0F, 0.0F };
    float extent[3] = { 1.0F, 1.0F, 1.0F };
    if (vertexCount != 0) {
        for (int component = 0; component < 3; ++component) {
            center[component] = (minPosition[component] + maxPosition[component]) * 0.5F;
            extent[component] = std::max((maxPosition[component] - minPosition[component]) * 0.5F, 1.0F);
        }
    }
    std::printf("[HARNESS] model bbox: x[%.0f..%.0f] y[%.0f..%.0f] z[%.0f..%.0f] (%zu verts)\n", minPosition[0],
                maxPosition[0], minPosition[1], maxPosition[1], minPosition[2], maxPosition[2], vertexCount);

    int horizontalAxis = 0;
    int verticalAxis = 1;
    int depthAxis = 2;
    if (fixture.viewPlane == "zy") {
        horizontalAxis = 2;
        depthAxis = 0;
    } else if (fixture.viewPlane == "xz") {
        verticalAxis = 2;
        depthAxis = 1;
    }

    const float fit = 0.8F / std::max(extent[horizontalAxis], extent[verticalAxis]);
    const float depthFit = 0.4F / extent[depthAxis];
    MtxF& projection = fixture.matrixReplacements[&fixture.projectionMatrix];
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            projection.mf[row][column] = row == column ? 1.0F : 0.0F;
        }
    }
    MtxF& modelView = fixture.matrixReplacements[&fixture.modelViewMatrix];
    std::memset(modelView.mf, 0, sizeof(modelView.mf));
    modelView.mf[horizontalAxis][0] = fit;
    modelView.mf[verticalAxis][1] = fit;
    modelView.mf[depthAxis][2] = depthFit;
    modelView.mf[3][0] = -center[horizontalAxis] * fit;
    modelView.mf[3][1] = -center[verticalAxis] * fit;
    modelView.mf[3][2] = 0.5F - center[depthAxis] * depthFit;
    modelView.mf[3][3] = 1.0F;

    ConfigureViewport(fixture);
    Gfx viewport = gsSPViewport(&fixture.viewport);
    Gfx scissor = gsDPSetScissor(G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    Gfx projectionCommand = gsSPMatrix(&fixture.projectionMatrix, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    Gfx modelViewCommand = gsSPMatrix(&fixture.modelViewMatrix, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    Gfx primitiveColor = gsDPSetPrimColor(0, 0, 255, 255, 255, 255);
    Gfx modelCall = gsSPDisplayList(fixture.model.data());
    Gfx end = gsSPEndDisplayList();
    fixture.commands = { viewport, scissor, projectionCommand, modelViewCommand, primitiveColor, modelCall, end };
    return true;
}

} // namespace Zelda3D::DlistHarness
