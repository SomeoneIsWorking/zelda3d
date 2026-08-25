#include "title_activity.h"

#include "../../core/zelda3d_runtime.h"
#include "../../diagnostics/boot_fixture.h"

#include <cstdlib>

extern "C" {
int gZelda3dTitleCam = 1;
}

namespace Zelda3D {

TitleActivity& TitleActivity::Instance() {
    static TitleActivity instance;
    return instance;
}

bool TitleActivity::shouldBeActive(const PlayState* play) const {
    return play != nullptr && Zelda3D_TitleCamEnabled() && !Zelda3D_AutoWarpEnabled() && play->sceneNum == SCENE_TITLE;
}

bool TitleActivity::activate() {
    const bool entered = !mEntered;
    mActive = true;
    mEntered = true;
    return entered;
}

bool TitleActivity::deactivate() {
    const bool wasActive = mActive;
    mActive = false;
    mEntered = false;
    return wasActive;
}

bool TitleActivity::resetRunState() {
    const bool inherited = mActive || mEntered;
    mActive = false;
    mEntered = false;
    return inherited;
}

bool TitleActivity::isActive() const {
    return mActive;
}

} // namespace Zelda3D

extern "C" int Zelda3D_TitleCamEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char* value = std::getenv("ZELDA3D_TITLECAM");
        enabled = (value != nullptr && value[0] == '0') ? 0 : 1;
    }
    return Zelda3D_Enabled() && enabled && gZelda3dTitleCam;
}

extern "C" int Zelda3D_Title_IsActive(void) {
    return Zelda3D::TitleActivity::Instance().isActive() ? 1 : 0;
}
