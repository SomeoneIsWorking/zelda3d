// OoT as a loadable game core.
//
// This is the body that used to be int main() in exe_entry.c. It moved here because the game now
// has two callers, not one: soh.elf runs it directly, and the launcher process dlopen's this same
// code as a shared object and calls it through Zelda3D_CoreEntry. Keeping ONE body means the
// dlopen'd path cannot quietly drift from the standalone path -- the standard failure of a
// second entry point that starts as a copy.
//
// Nothing here may be exported except Zelda3D_CoreEntry: the core is loaded RTLD_LOCAL, so its
// symbols are invisible by design, and that invisibility is what lets OoT and MM both define
// Play_Init in one process. See ship/zelda3d_core.h.

#include "global.h"
#include <soh/Enhancements/bootcommands.h>
#include "soh/OTRGlobals.h"
#include "soh/CrashHandlerExt.h"
#include <ship/zelda3d_core.h>

int Zelda3D_CoreRun(int argc, char* argv[]) {
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

static const Zelda3DCore sCore = {
    .abi = ZELDA3D_CORE_ABI,
    .id = "oot",
    .title = "The Legend of Zelda: Ocarina of Time",
    .run = Zelda3D_CoreRun,
};

const Zelda3DCore* Zelda3D_CoreEntry(void) {
    return &sCore;
}
