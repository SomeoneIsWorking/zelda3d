#include "menu_launcher.h"

#include "launcher_control.h"
#include "menu_navigation.h"
#include "randomizer_generation.h"

bool Zelda3D_MenuLauncherReplCommand(const char* command, const char* line, const char* outPath) {
    return Zelda3D_MenuNavigationReplCommand(command, line, outPath) ||
           Zelda3D_LauncherControlReplCommand(command, line, outPath) ||
           Zelda3D_RandomizerGenerationReplCommand(command, line, outPath);
}
