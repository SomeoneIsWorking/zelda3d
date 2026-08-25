#include "harness_options.h"

#include <cstdio>
#include <cstdlib>

extern "C" {
#ifdef HAVE_KIBAKO
extern Gfx zelda3d_kibako_model_dl[];
#endif
#ifdef HAVE_POT
extern Gfx zelda3d_pot_model_dl[];
#endif
#ifdef HAVE_GS
extern Gfx zelda3d_gs_model_dl[];
#endif
#ifdef HAVE_HINTSTONE
extern Gfx zelda3d_hintstone_model_dl[];
#endif
#ifdef HAVE_GELDWOMAN
extern Gfx zelda3d_geldwoman_model_dl[];
#endif
#ifdef HAVE_CHILDLINK
extern Gfx zelda3d_childlink_model_dl[];
#endif
}

namespace Zelda3D::DlistHarness {

HarnessOptions ParseHarnessOptions(int argc, char** argv) {
    HarnessOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--gpu") {
            options.gpuMode = true;
        } else if (argument == "--zelda3d") {
            options.zelda3dMode = true;
            options.gpuMode = true;
        } else if (argument == "--zar" && index + 1 < argc) {
            options.zarPath = argv[++index];
        } else if (argument == "--rotx" && index + 1 < argc) {
            options.rotationX = static_cast<float>(std::atof(argv[++index]));
        } else if (argument == "--roty" && index + 1 < argc) {
            options.rotationY = static_cast<float>(std::atof(argv[++index]));
        } else if (argument == "--rotz" && index + 1 < argc) {
            options.rotationZ = static_cast<float>(std::atof(argv[++index]));
        } else if (argument == "--out" && index + 1 < argc) {
            options.outputPath = argv[++index];
        } else if (argument == "--model" && index + 1 < argc) {
            options.modelName = argv[++index];
        } else if (argument == "--view" && index + 1 < argc) {
            options.viewPlane = argv[++index];
        } else if (argument == "--size" && index + 1 < argc) {
            unsigned width = 0;
            unsigned height = 0;
            if (std::sscanf(argv[++index], "%ux%u", &width, &height) == 2) {
                options.width = width;
                options.height = height;
            }
        }
    }
    if (options.outputPath.empty()) {
        options.outputPath =
            options.zelda3dMode ? "scratch/render/zelda3d_gpu.ppm" : "scratch/render/" + options.modelName + "_lus.ppm";
    }
    return options;
}

Gfx* SelectCompiledModel(const std::string& name) {
#ifdef HAVE_KIBAKO
    if (name == "kibako") {
        return zelda3d_kibako_model_dl;
    }
#endif
#ifdef HAVE_POT
    if (name == "pot") {
        return zelda3d_pot_model_dl;
    }
#endif
#ifdef HAVE_GS
    if (name == "gs") {
        return zelda3d_gs_model_dl;
    }
#endif
#ifdef HAVE_HINTSTONE
    if (name == "hintstone") {
        return zelda3d_hintstone_model_dl;
    }
#endif
#ifdef HAVE_GELDWOMAN
    if (name == "geldwoman") {
        return zelda3d_geldwoman_model_dl;
    }
#endif
#ifdef HAVE_CHILDLINK
    if (name == "childlink") {
        return zelda3d_childlink_model_dl;
    }
#endif
    return nullptr;
}

} // namespace Zelda3D::DlistHarness
