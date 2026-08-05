// The launcher process: one binary that runs either game.
//
// It contains NO game code. OoT and MM are shared objects (libsoh_core.so, libmm_core.so) loaded
// with dlopen(RTLD_NOW | RTLD_LOCAL); this process links only libultraship, which both cores also
// link, so the engine -- window, renderer, resource manager -- is genuinely one copy while the game
// code is two private ones (claims C050, C054).
//
// RTLD_LOCAL is the mechanism the whole design rests on. OoT and MM each define Play_Init,
// Actor_Draw and 6,614 more colliding decomp symbols; renaming them would wreck each tree's
// correspondence to its upstream decomp. RTLD_LOCAL keeps a core's symbols out of the global
// namespace instead, so both can be loaded at once and neither can see the other. The price is that
// this process cannot link against anything in a core: the entire surface is the one dlsym'd entry
// point in ship/zelda3d_core.h, and libultraship reaches back the other way through the registered
// hooks in ship/zelda3d_hostiface.h.
//
// NOT YET MOVED HERE: the RmlUi chooser, which still runs as an OoT gamestate inside the soh core
// (src/zelda3d/launcher/). Presenting it from this process means owning a Ship::Context before any
// core is loaded and handing it over -- the "Ship::Context ownership" half of N3 in
// docs/MM_NATIVE.md. Until then this launcher chooses from argv/env, which is what the headless
// tooling needs anyway.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <dlfcn.h>
#include <libgen.h>
#include <unistd.h>

#include <ship/Context.h>
#include <ship/zelda3d_core.h>

namespace {

struct CoreSpec {
    const char* id;
    const char* subdir;  // build tree layout: build-cmake/<subdir>/<file>
    const char* file;
    const char* envOverride;
    const char* buildTarget; // named in the error, so a missing core is actionable
};

// Every game this launcher knows how to start. Adding one is a row here plus a core that exports
// Zelda3D_CoreEntry -- nothing else in this file is per-game.
const CoreSpec kCores[] = {
    { "oot", "soh", "libsoh_core.so", "ZELDA3D_CORE_OOT", "soh_core" },
    { "mm", "mm", "libmm_core.so", "ZELDA3D_CORE_MM", "mm_core" },
};

const CoreSpec* FindSpec(const std::string& id) {
    for (const CoreSpec& spec : kCores) {
        if (id == spec.id) {
            return &spec;
        }
    }
    return nullptr;
}

// Where this executable lives. Core paths are resolved relative to it so a build tree, an install
// tree and a sibling build dir all work without configuration.
std::string SelfDir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) {
        return ".";
    }
    buf[n] = '\0';
    return dirname(buf);
}

// The build puts each core beside its own assets (build-cmake/soh/, build-cmake/mm/), and the
// launcher next to them. An explicit env override wins, matching the ZELDA3D_SOH / ZELDA3D_MM
// convention the game scripts already use for pointing at another build dir.
// Always ABSOLUTE. The launcher chdir's into the core's asset directory before loading it, so a
// relative path -- which the env override may well be -- would otherwise be resolved against the
// wrong directory by the dlopen that follows the chdir. Resolving once, up front, removes the
// ordering hazard instead of relying on nobody passing a relative path.
std::string ResolveCorePath(const CoreSpec& spec) {
    std::string path;
    if (const char* env = getenv(spec.envOverride); env != nullptr && env[0] != '\0') {
        path = env;
    } else {
        path = SelfDir() + "/../" + spec.subdir + "/" + spec.file;
    }

    // realpath fails when the core is missing; keep the unresolved path so the error message names
    // what the user actually asked for rather than an empty string.
    if (char* abs = realpath(path.c_str(), nullptr); abs != nullptr) {
        path = abs;
        free(abs);
    }
    return path;
}

// Load a core and hand back its descriptor. Every failure path says which core, which path and why:
// a launcher that reports "could not start" without naming the file it could not open turns a
// one-line fix into a debugging session.
const Zelda3DCore* LoadCore(const CoreSpec& spec, void** handleOut) {
    const std::string path = ResolveCorePath(spec);

    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        fprintf(stderr,
                "ZELDA3D LAUNCHER: cannot load the %s core.\n"
                "  path:  %s\n"
                "  dlopen: %s\n"
                "  build it with: cmake --build Shipwright/build-cmake --target %s\n"
                "  or point %s at an existing one.\n",
                spec.id, path.c_str(), dlerror(), spec.buildTarget, spec.envOverride);
        return nullptr;
    }

    dlerror(); // clear, so the NULL check below distinguishes "absent" from "resolved to NULL"
    auto entry = reinterpret_cast<Zelda3DCoreEntryFn>(dlsym(handle, ZELDA3D_CORE_ENTRY_SYMBOL));
    const char* symErr = dlerror();
    if (entry == nullptr || symErr != nullptr) {
        fprintf(stderr,
                "ZELDA3D LAUNCHER: %s loaded but exports no %s (%s).\n"
                "  That object is not a game core, or was built before the core ABI existed.\n",
                path.c_str(), ZELDA3D_CORE_ENTRY_SYMBOL, symErr != nullptr ? symErr : "symbol is NULL");
        dlclose(handle);
        return nullptr;
    }

    const Zelda3DCore* core = entry();
    if (core == nullptr || core->abi != ZELDA3D_CORE_ABI) {
        fprintf(stderr,
                "ZELDA3D LAUNCHER: %s has core ABI %d, this launcher speaks %d.\n"
                "  A stale core .so is being loaded; rebuild it.\n",
                path.c_str(), core != nullptr ? core->abi : -1, ZELDA3D_CORE_ABI);
        dlclose(handle);
        return nullptr;
    }

    if (handleOut != nullptr) {
        *handleOut = handle;
    }
    return core;
}

// Decomp symbols that OoT and MM BOTH define -- the collision the whole design exists to dissolve.
// If RTLD_LOCAL is working, each core's copy is private and the two addresses differ; one shared
// address would mean one game is calling the other's code.
const char* const kCollidingSymbols[] = { "Play_Init", "Actor_Draw", "Actor_Kill" };

// Diagnostic: load EVERY known core into this one process, verify their symbols stayed private, and
// exit. This is the check that two RTLD_LOCAL cores really do coexist -- the residual C054 recorded
// as untested, where static-initialisation order across the dlopen boundary is the risk.
//
// Two things make the negative visible rather than silent. It loads them ALL rather than stopping
// at the first success, so a second core that fails to map cannot be reported as a clean run; and
// the summary always states the denominator, so "loaded 1/2" cannot be misread as "fine".
//
// Loading is necessary but NOT sufficient, which is why the symbol check exists: two cores that
// loaded while resolving each other's Play_Init would look identical to success here, and that is
// precisely the failure RTLD_LOCAL is there to prevent.
int ProbeCores() {
    const int total = static_cast<int>(sizeof kCores / sizeof kCores[0]);
    const int symCount = static_cast<int>(sizeof kCollidingSymbols / sizeof kCollidingSymbols[0]);
    int loaded = 0;
    void* handles[total];

    printf("zelda3d: probing %d core(s), each dlopen'd RTLD_NOW | RTLD_LOCAL into this process\n", total);
    for (int i = 0; i < total; ++i) {
        const CoreSpec& spec = kCores[i];
        handles[i] = nullptr;
        const Zelda3DCore* core = LoadCore(spec, &handles[i]);
        if (core == nullptr) {
            printf("  %-4s FAILED (see above)\n", spec.id);
            continue;
        }
        ++loaded;
        printf("  %-4s ok   abi=%d id=%s title=\"%s\" run=%p handle=%p\n", spec.id, core->abi, core->id, core->title,
               reinterpret_cast<void*>(core->run), handles[i]);
    }

    // Symbol privacy, checked only when every core is up -- with one core loaded there is nothing to
    // collide with, and reporting "0 shared" then would be a pass the method could not have failed.
    int shared = 0;
    int compared = 0;
    if (loaded == total && total > 1) {
        printf("zelda3d: checking %d known-colliding decomp symbol(s) are PRIVATE per core\n", symCount);
        for (const char* sym : kCollidingSymbols) {
            void* first = dlsym(handles[0], sym);
            if (first == nullptr) {
                printf("  %-12s SKIPPED -- not found in %s, so nothing was compared\n", sym, kCores[0].id);
                continue;
            }
            for (int i = 1; i < total; ++i) {
                void* other = dlsym(handles[i], sym);
                if (other == nullptr) {
                    printf("  %-12s SKIPPED -- not found in %s, so nothing was compared\n", sym, kCores[i].id);
                    continue;
                }
                ++compared;
                if (first == other) {
                    ++shared;
                    printf("  %-12s SHARED at %p -- %s and %s resolved to ONE copy, isolation FAILED\n", sym, first,
                           kCores[0].id, kCores[i].id);
                } else {
                    printf("  %-12s private: %s=%p %s=%p\n", sym, kCores[0].id, first, kCores[i].id, other);
                }
            }
        }
        printf("zelda3d: %d symbol pair(s) compared, %d shared\n", compared, shared);
    }

    // Deliberately not dlclose'd: they stay mapped to the end of the probe, because coexistence is
    // the thing being measured. Unloading each before loading the next would prove nothing.
    printf("zelda3d: loaded %d/%d cores simultaneously\n", loaded, total);
    if (loaded != total) {
        return 1;
    }
    // A run that compared nothing is not a pass. With every core loaded there must be something to
    // compare, and silently reporting success on zero comparisons is how a broken check looks clean.
    if (total > 1 && compared == 0) {
        printf("zelda3d: NO symbol pairs could be compared -- the privacy check did not run\n");
        return 1;
    }
    return shared == 0 ? 0 : 1;
}

void Usage() {
    fprintf(stderr,
            "usage: zelda3d [oot|mm] [game args...]\n"
            "       zelda3d --probe-cores    load every core into one process and report\n"
            "\n"
            "The game may also be set with ZELDA3D_GAME. Default: oot.\n");
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc > 1 && strcmp(argv[1], "--probe-cores") == 0) {
        return ProbeCores();
    }
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        Usage();
        return 0;
    }

    // Game selection: first argument if it names one, else $ZELDA3D_GAME, else OoT. Consuming the
    // argument only when it IS a game id leaves every other argument for the core, so existing
    // command lines keep working through the launcher unchanged.
    std::string game = "oot";
    int gameArgc = argc;
    char** gameArgv = argv;
    if (const char* env = getenv("ZELDA3D_GAME"); env != nullptr && env[0] != '\0') {
        game = env;
    }
    if (argc > 1 && FindSpec(argv[1]) != nullptr) {
        game = argv[1];
        gameArgc = argc - 1;
        gameArgv = argv + 1;
    }

    const CoreSpec* spec = FindSpec(game);
    if (spec == nullptr) {
        fprintf(stderr, "ZELDA3D LAUNCHER: unknown game \"%s\".\n", game.c_str());
        Usage();
        return 2;
    }

    // The engine resolves archives and config relative to the CURRENT DIRECTORY (Context's app
    // directory falls through to "."), which is why run.sh cds into the build dir before launching
    // a game binary. The launcher inherits that contract rather than changing it: move to the
    // core's own directory, where its .o2r/.otr and assets are staged.
    const std::string corePath = ResolveCorePath(*spec);
    {
        std::string dir = corePath;
        char* buf = dir.data();
        if (chdir(dirname(buf)) != 0) {
            fprintf(stderr, "ZELDA3D LAUNCHER: cannot enter the %s core's asset directory (%s).\n", spec->id,
                    corePath.c_str());
            return 3;
        }
    }

    void* handle = nullptr;
    const Zelda3DCore* core = LoadCore(*spec, &handle);
    if (core == nullptr) {
        return 4;
    }

    // Tell the engine where the running game's data lives. libultraship otherwise derives this from
    // the EXECUTABLE, which is this launcher -- and the launcher's directory holds no fonts, no
    // RmlUi assets and no extractor assets/ folder, so OoT aborted at startup reporting them
    // missing. dladdr on the entry point reports where the core was ACTUALLY loaded from, following
    // whatever ld.so resolved, rather than re-deriving a path that could disagree with it.
    if (Dl_info info; dladdr(reinterpret_cast<void*>(core->run), &info) != 0 && info.dli_fname != nullptr) {
        std::string dir = info.dli_fname;
        char* buf = dir.data();
        Ship::Context::SetAppBundlePath(dirname(buf));
    } else {
        // Leaving it unset would send the engine to the launcher's directory and fail obscurely much
        // later, at the first asset it could not find.
        fprintf(stderr, "ZELDA3D LAUNCHER: cannot locate the loaded %s core on disk (dladdr failed); "
                        "its assets would be looked for beside this launcher.\n",
                spec->id);
        return 5;
    }

    fprintf(stderr, "ZELDA3D LAUNCHER: starting %s (%s)\n", core->title, core->id);
    fflush(stderr);

    // The core owns the process from here: InitOTR, the frame loop and teardown, exactly as when it
    // is run as a standalone binary. No dlclose afterwards -- unloading a core whose static
    // destructors and background threads have already run is a way to crash on the way out, and the
    // process is exiting regardless.
    return core->run(gameArgc, gameArgv);
}
