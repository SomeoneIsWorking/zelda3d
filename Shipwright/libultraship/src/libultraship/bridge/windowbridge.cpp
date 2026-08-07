#include "libultraship/bridge/windowbridge.h"
#include "ship/window/Window.h"
#include "ship/Context.h"

extern "C" {

uint32_t WindowGetWidth() {
    return Ship::Context::GetRawInstance()->GetWindow()->GetWidth();
}

uint32_t WindowGetHeight() {
    return Ship::Context::GetRawInstance()->GetWindow()->GetHeight();
}

float WindowGetAspectRatio() {
    return Ship::Context::GetRawInstance()->GetWindow()->GetCurrentAspectRatio();
}

void WindowRequestExit() {
    Ship::Context::RequestExit();
}

void WindowRequestExitWithFullTeardown() {
    Ship::Context::RequestExitWithFullTeardown();
}

void WindowRequestGameSwitch(const char* gameId) {
    if (gameId == nullptr || gameId[0] == '\0') {
        return;
    }
    Ship::Context::RequestGameSwitch(gameId);
    Ship::Context::RequestExit();
}

bool WindowIsRunning() {
    // Checked BEFORE the backend, and deliberately at this bridge rather than inside a window
    // backend: both games' graph loops are `while (WindowIsRunning()) RunFrame();`, so one check
    // here reaches OoT and MM without either backend or either decomp knowing about it. An exit
    // request then unwinds through the same path a closed window takes.
    if (Ship::Context::IsExitRequested()) {
        return false;
    }
    return Ship::Context::GetRawInstance()->GetWindow()->IsRunning();
}

int32_t WindowGetPosX() {
    return Ship::Context::GetRawInstance()->GetWindow()->GetPosX();
}

int32_t WindowGetPosY() {
    return Ship::Context::GetRawInstance()->GetWindow()->GetPosY();
}

bool WindowIsFullscreen() {
    return Ship::Context::GetRawInstance()->GetWindow()->IsFullscreen();
}
}
