#include "window_session.h"

namespace {

Fast::Fast3dWindow* sFast3dWindow = nullptr;

} // namespace

void Zelda3D_SetFast3dWindow(Fast::Fast3dWindow* window) {
    sFast3dWindow = window;
}

Fast::Fast3dWindow* Zelda3D_GetFast3dWindow() {
    return sFast3dWindow;
}
