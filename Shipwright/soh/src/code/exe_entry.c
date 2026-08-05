// Executable entry point for the soh.elf binary — OoT started directly, without the launcher.
//
// The game itself lives in src/code/core_entry.c (Zelda3D_CoreRun), which the launcher reaches by
// dlopen instead. This TU is only the process-level shell: the platform entry symbol and, on
// Windows, the console the extractor needs. Kept in its own TU so a binary supplying its own driver
// — the direct harness in tools/soh3d_harness/, which alternates SoH3D frames with Azahar frames —
// can link soh_lib without pulling in a competing int main().
#ifdef _WIN32
#include <Windows.h>
#include <stdio.h>
#include <locale.h>
#endif

int Zelda3D_CoreRun(int argc, char* argv[]);

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
    return Zelda3D_CoreRun(argc, argv);
}
