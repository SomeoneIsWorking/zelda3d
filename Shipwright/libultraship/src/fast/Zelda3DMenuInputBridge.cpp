#include "fast/Fast3dGui.h"

#include "ship/Context.h"
#include "ship/window/Window.h"

#include <ship/zelda3d_menu_input.h>

#include <SDL3/SDL_keycode.h>

namespace {

Fast::Fast3dGui* GetFast3dGui() {
    auto context = Ship::Context::GetRawInstance();
    if (context == nullptr || context->GetWindow() == nullptr) {
        return nullptr;
    }
    return dynamic_cast<Fast::Fast3dGui*>(context->GetWindow()->GetGui().get());
}

SDL_Keycode MenuActionKey(int action) {
    switch (action) {
        case 0:
            return SDLK_DOWN;
        case 1:
            return SDLK_UP;
        case 2:
            return SDLK_RETURN;
        case 4:
            return SDLK_RIGHT;
        case 5:
            return SDLK_LEFT;
        default:
            return SDLK_ESCAPE;
    }
}

} // namespace

extern "C" void Zelda3D_RmlMenuKey(int action) {
    Fast::Fast3dGui* gui = GetFast3dGui();
    if (gui != nullptr) {
        gui->RmlMenuInjectKey(MenuActionKey(action));
    }
}

extern "C" void Zelda3D_RmlMenuClick(int x, int y) {
    Fast::Fast3dGui* gui = GetFast3dGui();
    if (gui != nullptr) {
        gui->RmlMenuInjectClick(x, y);
    }
}
