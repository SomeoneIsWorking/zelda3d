#if defined(__ANDROID__) || defined(__IOS__)
#include "ship/port/mobile/MobileImpl.h"
#include <SDL3/SDL.h>

#include <imgui_internal.h>

static bool isShowingVirtualKeyboard = true;

void Ship::Mobile::ImGuiProcessEvent(bool wantsTextInput) {
    ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetActiveID());

    if (wantsTextInput) {
        if (!isShowingVirtualKeyboard) {
            state->ClearText();

            isShowingVirtualKeyboard = true;
            // SDL3-MIGRATION: SDL_StartTextInput/SDL_StopTextInput now take an SDL_Window*; target the focused window.
            SDL_StartTextInput(SDL_GetKeyboardFocus());
        }
    } else {
        if (isShowingVirtualKeyboard) {
            isShowingVirtualKeyboard = false;
            SDL_StopTextInput(SDL_GetKeyboardFocus());
        }
    }
}
#endif
