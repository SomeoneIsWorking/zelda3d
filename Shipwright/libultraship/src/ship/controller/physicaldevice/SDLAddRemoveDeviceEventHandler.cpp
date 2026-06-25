#include "ship/controller/physicaldevice/SDLAddRemoveDeviceEventHandler.h"
#include <SDL3/SDL.h>
#include "ship/Context.h"
#include "ship/controller/controldeck/ControlDeck.h"

namespace Ship {

SDLAddRemoveDeviceEventHandler::~SDLAddRemoveDeviceEventHandler() {
}

void SDLAddRemoveDeviceEventHandler::InitElement() {
}

void SDLAddRemoveDeviceEventHandler::DrawElement() {
}

void SDLAddRemoveDeviceEventHandler::UpdateElement() {
    SDL_PumpEvents();
    SDL_Event event;
    // SDL3-MIGRATION: SDL_CONTROLLERDEVICEADDED/REMOVED -> SDL_EVENT_GAMEPAD_ADDED/REMOVED, and the
    // SDL_ControllerDeviceEvent payload is now event.gdevice (SDL_GamepadDeviceEvent). NOTE: in SDL3
    // .which is the joystick INSTANCE ID for both add and remove (in SDL2 the add event carried a device
    // index). Refresh handlers re-scan all devices and ignore the argument, so behaviour is unchanged.
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_GAMEPAD_ADDED, SDL_EVENT_GAMEPAD_ADDED) > 0) {
        Context::GetRawInstance()->GetControlDeck()->GetConnectedPhysicalDeviceManager()->HandlePhysicalDeviceConnect(
            event.gdevice.which);
    }

    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_GAMEPAD_REMOVED, SDL_EVENT_GAMEPAD_REMOVED) > 0) {
        Context::GetRawInstance()
            ->GetControlDeck()
            ->GetConnectedPhysicalDeviceManager()
            ->HandlePhysicalDeviceDisconnect(event.gdevice.which);
    }
}
} // namespace Ship
