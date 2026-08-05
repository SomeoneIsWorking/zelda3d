// Executable entry point for the mm.elf binary — MM started directly, without the launcher.
//
// The game itself is Zelda3D_CoreRun in src/code/main.c, which the launcher reaches by dlopen
// instead (libmm_core.so). This TU is only the process-level shell: the platform entry symbol.
// Kept separate so the core shared object carries no int main() of its own.

int Zelda3D_CoreRun(int argc, char* argv[]);

#ifdef __GNUC__
#define SDL_main main
#endif

int SDL_main(int argc, char* argv[]) {
    return Zelda3D_CoreRun(argc, argv);
}
