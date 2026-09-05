#pragma once

#include <ship/Context.h>
#include <ship/window/Window.h>
#include <libultraship/libultraship.h>

#define CMD_REGISTER Ship::Context::GetRawInstance()->GetConsole()->AddCommand
#define ERROR_MESSAGE                                                                    \
    std::reinterpret_pointer_cast<Ship::ConsoleWindow>(                                  \
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGuiWindow("Console")) \
        ->SendErrorMessage
#define INFO_MESSAGE                                                                     \
    std::reinterpret_pointer_cast<Ship::ConsoleWindow>(                                  \
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGuiWindow("Console")) \
        ->SendInfoMessage

void DebugConsole_RegisterPlayerCommands();
void DebugConsole_RegisterRandomizerCommands();
