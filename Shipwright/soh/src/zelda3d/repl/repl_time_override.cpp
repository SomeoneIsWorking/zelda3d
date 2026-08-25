#include "repl_time_override.h"

#include "../render/render_time_control.h"
#include "../scene/scene_time.h"
#include "global.h"

namespace Zelda3D::Repl {

void ApplyTimeOverride() {
    Zelda3D_InitForceTime();
    if (gZelda3dForceTime >= 0) {
        gSaveContext.dayTime = static_cast<u16>(gZelda3dForceTime);
        gSaveContext.skyboxTime = static_cast<u16>(gZelda3dForceTime);
    }
}

} // namespace Zelda3D::Repl
