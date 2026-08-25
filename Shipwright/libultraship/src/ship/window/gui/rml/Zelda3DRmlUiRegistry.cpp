#include "Zelda3DRmlUiRegistry.h"

namespace {

Ship::SohRmlUi* sLiveMenu = nullptr;

} // namespace

namespace Ship::Zelda3DRmlUiRegistry {

void Attach(SohRmlUi* menu) {
    sLiveMenu = menu;
}

void Detach(const SohRmlUi* menu) {
    if (sLiveMenu == menu) {
        sLiveMenu = nullptr;
    }
}

SohRmlUi* Get() {
    return sLiveMenu;
}

} // namespace Ship::Zelda3DRmlUiRegistry
