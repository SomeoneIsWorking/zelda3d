#include "boss_fd2_material_controller.h"

#include <cassert>

using Zelda3D::BossFd2Materials::Controller;

int main() {
    Controller controller;

    // The 3:2 scheduler alternates one and two OoT3D draws per SoH tick. Body/hair use step 2.
    controller.tick(true, false);
    assert(controller.bodyAndHairFrame(120) == 2.0f);
    controller.tick(true, false);
    assert(controller.bodyAndHairFrame(120) == 6.0f);

    // Hidden BossFd2 draws do not advance material instances, but cadence phase keeps running.
    controller.tick(false, false);
    assert(controller.bodyAndHairFrame(120) == 6.0f);
    controller.tick(true, false);
    assert(controller.bodyAndHairFrame(120) == 10.0f);

    // faceExposed resets the one-step pulse at the event and freezes it again when cleared.
    controller.tick(true, true);
    assert(controller.faceExposed());
    assert(controller.pulseFrame(12) == 1.0f);
    controller.tick(true, true);
    assert(controller.pulseFrame(12) == 3.0f);
    controller.tick(true, false);
    assert(!controller.faceExposed());
    assert(controller.pulseFrame(12) == 3.0f);
    controller.tick(true, true);
    assert(controller.pulseFrame(12) == 2.0f);

    return 0;
}
