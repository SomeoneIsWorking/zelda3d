#pragma once

#include <cstdint>
#include <string>

#include <libultraship/libultra/gbi.h>

namespace Zelda3D::DlistHarness {

struct HarnessOptions {
    bool gpuMode = false;
    bool zelda3dMode = false;
    std::string outputPath;
    std::string modelName = "kibako";
    std::string viewPlane = "xy";
    std::string zarPath = "/actor/zelda_ge1.zar";
    float rotationX = 0.0F;
    float rotationY = 0.0F;
    float rotationZ = 0.0F;
    uint32_t width = 640;
    uint32_t height = 480;
};

HarnessOptions ParseHarnessOptions(int argc, char** argv);
Gfx* SelectCompiledModel(const std::string& name);

} // namespace Zelda3D::DlistHarness
