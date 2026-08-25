#include "Zelda3DRmlUiRegistry.h"

#include "SohRmlUi.h"

#include <ship/zelda3d_launcher_bridge.h>

#include <cstdio>

extern "C" int gZelda3dLauncherAction = 0;

extern "C" void Zelda3D_LauncherShow(int show) {
    Ship::SohRmlUi* menu = Ship::Zelda3DRmlUiRegistry::Get();
    if (menu != nullptr) {
        menu->ShowLauncher(show != 0);
    }
}

extern "C" int Zelda3D_LauncherIsVisible(void) {
    const Ship::SohRmlUi* menu = Ship::Zelda3DRmlUiRegistry::Get();
    return menu != nullptr && menu->IsLauncherVisible() ? 1 : 0;
}

extern "C" void Zelda3D_LauncherHitReport(char* out, int outSize) {
    if (out == nullptr || outSize <= 0) {
        return;
    }
    Ship::SohRmlUi* menu = Ship::Zelda3DRmlUiRegistry::Get();
    if (menu == nullptr) {
        std::snprintf(out, outSize, "launcher hit-test UNAVAILABLE: no live RmlUi instance -- NOTHING was tested");
        return;
    }
    menu->DescribeLauncherHits(out, outSize);
}
