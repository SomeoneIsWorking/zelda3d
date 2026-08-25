#include "sdl3gpu_headless_environment.h"

#include <cstdlib>

namespace Zelda3D::DlistHarness {

void ConfigureHeadlessSdl3GpuEnvironment() {
    setenv("SOH_HEADLESS", "1", 1);
}

} // namespace Zelda3D::DlistHarness
