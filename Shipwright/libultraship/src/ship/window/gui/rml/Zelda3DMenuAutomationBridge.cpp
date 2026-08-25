#include "Zelda3DRmlUiRegistry.h"

#include "SohRmlUi.h"

#include <ship/zelda3d_menu_input.h>

#include <cstdio>

extern "C" void Zelda3D_MenuActivateRow(const char* needle, char* out, int outSize) {
    if (out == nullptr || outSize <= 0) {
        return;
    }
    Ship::SohRmlUi* menu = Ship::Zelda3DRmlUiRegistry::Get();
    if (menu == nullptr) {
        std::snprintf(out, outSize, "menurow UNAVAILABLE: no live RmlUi instance -- NOTHING activated");
        return;
    }
    menu->ActivateRowByLabel(needle, out, outSize);
}
