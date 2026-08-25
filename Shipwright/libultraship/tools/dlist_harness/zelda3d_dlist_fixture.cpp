#include "zelda3d_dlist_fixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <fast/interpreter.h>

#include "anim/authored_playback.h"
#include "asset/cmb.h"
#include "asset/csab.h"
#include "asset/ctr_rom.h"
#include "asset/zar.h"
#include "render/model_queries.h"

namespace Zelda3D::DlistHarness {
namespace {

float AnimationFrame() {
    const char* frame = std::getenv("ZELDA3D_FRAME");
    return frame != nullptr ? static_cast<float>(std::atof(frame)) : 0.0F;
}

void MeasureBounds(const std::vector<Zelda3D::CmbDrawGroup>& groups, float lower[3], float upper[3]) {
    for (const Zelda3D::CmbDrawGroup& group : groups) {
        for (const auto& vertex : group.verts) {
            for (int component = 0; component < 3; ++component) {
                lower[component] = std::min(lower[component], vertex.pos[component]);
                upper[component] = std::max(upper[component], vertex.pos[component]);
            }
        }
    }
}

std::vector<Zelda3D::CmbDrawGroup> BuildPoseGroups(const Zelda3D::Zar& archive, const Zelda3D::Cmb& model,
                                                   const char* animationName) {
    if (animationName == nullptr || *animationName == '\0') {
        return {};
    }
    const std::string requestedAnimation(animationName);
    const std::string fullName =
        requestedAnimation.rfind("Anim/", 0) == 0 ? requestedAnimation : "Anim/" + requestedAnimation + ".csab";
    const Zelda3D::ZarFile* animationFile = nullptr;
    for (const Zelda3D::ZarFile& file : archive.files()) {
        if (file.name == fullName) {
            animationFile = &file;
            break;
        }
    }
    if (animationFile == nullptr) {
        return {};
    }

    Zelda3D::Csab animation(archive.read(*animationFile));
    if (!animation.ok()) {
        return {};
    }
    const float frame = AnimationFrame();
    std::vector<std::array<float, 16>> skinMatrices;
    animation.skinMatrices(model, frame, skinMatrices);
    return model.buildDrawGroupsSkinned(skinMatrices.data(), skinMatrices.size());
}

void MultiplyMatrices(const float left[3][3], const float right[3][3], float output[3][3]) {
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            output[row][column] = 0.0F;
            for (int component = 0; component < 3; ++component) {
                output[row][column] += left[row][component] * right[component][column];
            }
        }
    }
}

void ConfigureMatrices(DlistFixture& fixture, const float center[3], const float extent[3], float rotationX,
                       float rotationY, float rotationZ) {
    const float fit = 0.8F / std::max(extent[0], extent[1]);
    const float scales[3] = { fit, fit, 0.4F / extent[2] };
    const auto radians = [](float degrees) { return degrees * 3.14159265358979F / 180.0F; };
    const float cosineX = std::cos(radians(rotationX));
    const float sineX = std::sin(radians(rotationX));
    const float cosineY = std::cos(radians(rotationY));
    const float sineY = std::sin(radians(rotationY));
    const float cosineZ = std::cos(radians(rotationZ));
    const float sineZ = std::sin(radians(rotationZ));
    const float matrixX[3][3] = { { 1, 0, 0 }, { 0, cosineX, -sineX }, { 0, sineX, cosineX } };
    const float matrixY[3][3] = { { cosineY, 0, sineY }, { 0, 1, 0 }, { -sineY, 0, cosineY } };
    const float matrixZ[3][3] = { { cosineZ, -sineZ, 0 }, { sineZ, cosineZ, 0 }, { 0, 0, 1 } };
    float matrixZY[3][3];
    float rotation[3][3];
    MultiplyMatrices(matrixZ, matrixY, matrixZY);
    MultiplyMatrices(matrixZY, matrixX, rotation);

    MtxF& projection = fixture.matrixReplacements[&fixture.projectionMatrix];
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            projection.mf[row][column] = row == column ? 1.0F : 0.0F;
        }
    }
    MtxF& modelView = fixture.matrixReplacements[&fixture.modelViewMatrix];
    std::memset(modelView.mf, 0, sizeof(modelView.mf));
    constexpr float biases[3] = { 0.0F, 0.0F, 0.5F };
    for (int column = 0; column < 3; ++column) {
        for (int component = 0; component < 3; ++component) {
            modelView.mf[component][column] = scales[column] * rotation[column][component];
        }
        float translation = biases[column];
        for (int component = 0; component < 3; ++component) {
            translation -= modelView.mf[component][column] * center[component];
        }
        modelView.mf[3][column] = translation;
    }
    modelView.mf[3][3] = 1.0F;
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

void ConfigureCanary(DlistFixture& fixture, const float lower[3], const float center[3], const float extent[3]) {
    const auto delta = static_cast<int16_t>(extent[0] * 0.05F);
    const auto baseX = static_cast<int16_t>(lower[0]);
    const auto baseY = static_cast<int16_t>(lower[1]);
    const auto baseZ = static_cast<int16_t>(center[2]);
    fixture.canary[0].v.ob[0] = baseX;
    fixture.canary[0].v.ob[1] = baseY;
    fixture.canary[0].v.ob[2] = baseZ;
    fixture.canary[1].v.ob[0] = baseX + delta;
    fixture.canary[1].v.ob[1] = baseY;
    fixture.canary[1].v.ob[2] = baseZ;
    fixture.canary[2].v.ob[0] = baseX;
    fixture.canary[2].v.ob[1] = baseY + delta;
    fixture.canary[2].v.ob[2] = baseZ;
    for (Vtx& vertex : fixture.canary) {
        vertex.v.cn[0] = 255;
        vertex.v.cn[1] = 0;
        vertex.v.cn[2] = 0;
        vertex.v.cn[3] = 255;
    }
}

void BuildCommands(DlistFixture& fixture, int modelId) {
    Gfx viewport = gsSPViewport(&fixture.viewport);
    Gfx scissor = gsDPSetScissor(G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    Gfx projection = gsSPMatrix(&fixture.projectionMatrix, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    Gfx modelView = gsSPMatrix(&fixture.modelViewMatrix, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    Gfx combine = gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE);
    Gfx canaryVertices = gsSPVertex(fixture.canary.data(), 3, 0);
    Gfx canaryTriangle = gsSP1Triangle(0, 1, 2, 0);
    Gfx modelDraw;
    gSPZelda3DDraw(&modelDraw, modelId, 255, 255, 255);
    Gfx end = gsSPEndDisplayList();
    fixture.commands = { viewport,       scissor,   projection,     modelView,      combine, canaryVertices,
                         canaryTriangle, modelDraw, canaryVertices, canaryTriangle, end };
}

} // namespace

bool BuildZelda3DDlistFixture(DlistFixture& fixture, const std::string& zarPath, int modelId, float rotationX,
                              float rotationY, float rotationZ) {
    const char* romPath = std::getenv("ZELDA3D_OOT3D_ROM");
    if (romPath == nullptr || *romPath == '\0') {
        std::fprintf(stderr, "[HARNESS] ZELDA3D_OOT3D_ROM not set\n");
        return false;
    }
    Zelda3D::CtrRom rom(romPath);
    if (!rom.ok()) {
        std::fprintf(stderr, "[HARNESS] CtrRom: %s\n", rom.error().c_str());
        return false;
    }
    auto archiveBytes = rom.read(zarPath);
    if (archiveBytes.empty()) {
        std::fprintf(stderr, "[HARNESS] zar not found: %s\n", zarPath.c_str());
        return false;
    }
    Zelda3D::Zar archive(std::move(archiveBytes));
    const Zelda3D::ZarFile* modelFile = archive.ok() ? archive.firstWithSuffix(".cmb") : nullptr;
    if (modelFile == nullptr) {
        std::fprintf(stderr, "[HARNESS] no .cmb in %s\n", zarPath.c_str());
        return false;
    }
    Zelda3D::Cmb model(archive.read(*modelFile));
    if (!model.ok()) {
        std::fprintf(stderr, "[HARNESS] Cmb: %s\n", model.error().c_str());
        return false;
    }

    const char* animationName = std::getenv("ZELDA3D_ANIM");
    std::vector<Zelda3D::CmbDrawGroup> groups = BuildPoseGroups(archive, model, animationName);
    if (groups.empty()) {
        groups = model.buildDrawGroups();
    }
    float lower[3] = { 1e30F, 1e30F, 1e30F };
    float upper[3] = { -1e30F, -1e30F, -1e30F };
    MeasureBounds(groups, lower, upper);
    float center[3];
    float extent[3];
    for (int component = 0; component < 3; ++component) {
        center[component] = (lower[component] + upper[component]) * 0.5F;
        extent[component] = std::max((upper[component] - lower[component]) * 0.5F, 1.0F);
    }
    std::printf("[HARNESS] zelda3d bbox x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f]\n", lower[0], upper[0], lower[1],
                upper[1], lower[2], upper[2]);

    ConfigureMatrices(fixture, center, extent, rotationX, rotationY, rotationZ);
    ConfigureViewport(fixture);
    ConfigureCanary(fixture, lower, center, extent);
    Zelda3D_EnsureModelProvider();
    if (animationName != nullptr && *animationName != '\0') {
        Zelda3D_UpdateAnim(modelId, animationName, AnimationFrame());
    }
    BuildCommands(fixture, modelId);
    return true;
}

} // namespace Zelda3D::DlistHarness
