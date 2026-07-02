// Executable entry point for the soh.elf binary. Kept in a separate TU
// so a direct-harness binary (see tools/soh3d_harness/) can link soh_lib
// without pulling in a competing int main() — the harness supplies its
// own driver that alternates SoH3D frames (Main_Init + RunFrame +
// Main_Shutdown, all in soh_lib) with Azahar frames.
#ifdef _WIN32
#include <Windows.h>
#include <locale.h>
#endif

#include "global.h"
#include <soh/Enhancements/bootcommands.h>
#include "soh/OTRGlobals.h"
#include "soh/CrashHandlerExt.h"

#ifdef _WIN32
// SDL3-MIGRATION: SDL2main (which provided the WinMain shim that calls SDL_main) was removed in SDL3.
// On Linux (the #else branch below) a plain int main(...) is the real entry point and needs no SDL.
// For the Windows build, SDL3's header-only SDL_main.h must be included in exactly one TU to map
// the real entry point onto this SDL_main; keep the SDL_main name here for that path.
int SDL_main(int argc, char* argv[]) {
    AllocConsole();
    (void)freopen("CONIN$", "r", stdin);
    (void)freopen("CONOUT$", "w", stdout);
    (void)freopen("CONOUT$", "w", stderr);
#ifndef _DEBUG
    ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
    // Allow non-ascii characters for Windows
    setlocale(LC_ALL, ".UTF8");

#else //_WIN32
int main(int argc, char* argv[]) {
#endif
    GameConsole_Init();
    InitOTR(argc, argv);
    // TODO: Was moved to below InitOTR because it requires window to be setup. But will be late to catch crashes.
    CrashHandlerRegisterCallback(CrashHandler_PrintSohData);
    BootCommands_Init();

    Heaps_Alloc();
    Main(0);
    DeinitOTR();
    Heaps_Free();
    return 0;
}
