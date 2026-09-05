#include "BenPort.h"

#include <ship/Context.h>
#include <ship/controller/controldeck/ControlDeck.h>
#include <ship/controller/controldevice/controller/Controller.h>

namespace {

Color_RGB8 GetColorForControllerLED() {
    return { 0, 0, 0 };
}

} // namespace

extern "C" void OTRControllerCallback(uint8_t rumble) {
    // SDL coalesces these per-tick requests and prevents driver spam.
    Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0)->GetLED()->SetLEDColor(
        GetColorForControllerLED());

    if (rumble) {
        Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0)->GetRumble()->StartRumble();
    } else {
        Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0)->GetRumble()->StopRumble();
    }
}

extern "C" int Controller_ShouldRumble(size_t slot) {
    if (Ship::Context::GetRawInstance()
            ->GetControlDeck()
            ->GetControllerByPort(static_cast<uint8_t>(slot))
            ->GetRumble()
            ->GetAllRumbleMappings()
            .empty()) {
        return 0;
    }

    if (Ship::Context::GetRawInstance()
            ->GetControlDeck()
            ->GetConnectedPhysicalDeviceManager()
            ->GetConnectedSDLGamepadsForPort(slot)
            .empty()) {
        return 0;
    }

    return 1;
}
