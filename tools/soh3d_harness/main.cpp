// soh3d_harness — headless libretro-frontend host that drives Azahar's
// libretro core in-process. Azahar's citra_libretro source files are linked
// directly into this executable; the SoH side uses the already-built shipping
// shared core rather than compiling a private duplicate game.
//
// This is the C++ side of the "direct harness" direction laid out in
// soh3d/CLAUDE.md ("Direction: build a direct harness that EMBEDS Azahar
// as a library, not runs it"). It keeps the C++ deliberately small —
// just a REPL that exposes retro_run + Azahar's Memory::MemorySystem +
// save-state I/O — so warp injection, actor-table dumps, and SoH3D
// side-by-side compare can live as Python scripts in tools/ instead of
// being baked into the binary. Matches how tools/soh3d_repl.py drives
// the SoH3D game today.
//
// Protocol: newline-delimited text on stdin/stdout. Two tiers:
//
// LOW-LEVEL primitives (free-form poking):
//   run <N>              -> ok run <N>
//   r8|r16|r32 <va>      -> ok <hex>            (or err)
//   w8|w16|w32 <va> <v>  -> ok
//   mem <va> <n>         -> ok <hex-bytes>      (or err)
//   loadstate <path>     -> ok  (or err)
//   savestate <path>     -> ok  (or err)
//   input <mask>         -> ok
//        held button mask (persists across run) — bit N = joypad id N
//        (B=0,Y=1,SELECT=2,START=3,UP=4,DOWN=5,LEFT=6,RIGHT=7,
//         A=8,X=9,L=10,R=11,L2=12,R2=13,L3=14,R3=15)
//
// HIGH-LEVEL OoT3D ops (RE knowledge lives here, not in scripts):
//   playstate            -> ok 0x<ptr> mode=play|title (or err "not populated")
//   gameplay             -> ok yes|no  (real gameplay scene, not the title demo)
//   scene                -> ok 0x<sceneNum>      (or err)
//   warp <entrance>      -> ok warp 0x<entrance> (writes nextEntranceIndex
//                                                 + transitionTrigger=20)
//   actors               -> ok actors <N>\n<one line per actor>\nok end
//                           (each line: cat id addr px py pz rx ry rz)
//
// SOH3D bring-up (embedded in the same process):
//   soh_boot             -> ok soh_boot   (GameConsole_Init + InitOTR +
//                                          BootCommands_Init + Heaps_Alloc +
//                                          Main_Init; SOH3D_HEADLESS forced)
//   soh_step <N>         -> ok soh_step <N>  (RunFrame() x N — advance
//                                              SoH3D's Graph state machine)
//   step <N>             -> ok step <N> <mode> [titlesync=HOLD|LOCKED]
//                           Combined driver — DEFAULT title-sync engages on
//                           the first call (see title_sync.h): the oracle
//                           loads scratch/title_settled.state, its RE'd
//                           title-cs cursor (u32 @0x0054CC3C) is read as the
//                           integer lock target, and it holds there while
//                           SoH boots cold; the first frame SoH's own cursor
//                           (Zelda3D_TitleCsFrame()) reaches that value the
//                           controller LOCKs and steps the oracle per-frame
//                           under a 0/1/2-step integer governor that keeps
//                           the modular cursor delta at 0 (wrap-safe — no
//                           content search, no resync). No-op (legacy
//                           passthrough) if loadstate/soh_boot already ran
//                           manually.
//   titlesync             -> ok titlesync state=... sohFrame=... azFrame=...
//
// Meta:
//   quit                 -> ok  (then exit)
//   help                 -> ok  (prints command list to stderr)
//
// All numeric args accept 0x prefix. On startup the harness prints
// `boot succeeded` to stdout once retro_load_game returns true, and
// then waits for commands on stdin. Send `quit` to exit cleanly.
//
// Usage:
//   soh3d_harness [rom_path]
//   soh3d_harness                        # rom = $ZELDA3D_OOT3D_ROM
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "binary_file.h"
#include "frontend_input.h"
#include "frontend_presentation.h"
#include "harness_repl.h"
#include "libretro_callbacks.h"
#include "libretro_frontend.h"
#include "frame_watchdog.h"
#include "process_environment.h"
#include "soh_runtime_bridge.h"
#include "texpack_setup.h"
#include "libretro.h"

// Azahar's own logging backend (Common::Log). retro_init() sets the global
// filter to Level::Debug, which floods stderr with per-frame spam like
// "Audio.DSP <Debug> mixers remaining_dirty=..." — thousands of synchronous
// stderr writes per second that measurably throttle the harness. We override
// the filter to Warning right after retro_init (see main()).
#include "common/logging/backend.h"
#include "common/logging/filter.h"
#include "common/logging/types.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <thread>
#include <unistd.h>

namespace {

std::thread g_worker_thread;

} // namespace

int main(int argc, char** argv) {
    if (!HarnessProcess::LoadRepoEnvironment())
        return EXIT_FAILURE;
    if (!HarnessProcess::AcquireSingletonLock())
        return EXIT_FAILURE;
    HarnessWatchdog::Install();
    // Keep stdout clean for the REPL wire — any SoH log before InitLogging
    // goes to stderr instead. Must run before Main_Init / InitOTR.
    Ship_EarlyLogToStderr();
    std::string rom_path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) == 0) {
            std::fprintf(stderr, "soh3d_harness: unknown option: %s\n", a.c_str());
            return EXIT_FAILURE;
        }
        if (rom_path.empty())
            rom_path = a;
    }
    if (rom_path.empty()) {
        if (const char* env = std::getenv("ZELDA3D_OOT3D_ROM"); env && *env) {
            rom_path = env;
        } else {
            std::fprintf(stderr, "soh3d_harness: no ROM (argv or $ZELDA3D_OOT3D_ROM)\n");
            return EXIT_FAILURE;
        }
    }

    HarnessFrontend::ConfigureDirectories();

    std::fprintf(stderr, "soh3d_harness: rom = %s\n", rom_path.c_str());

    retro_system_info sysinfo{};
    retro_get_system_info(&sysinfo);
    std::fprintf(stderr, "soh3d_harness: core = %s %s\n", sysinfo.library_name ? sysinfo.library_name : "?",
                 sysinfo.library_version ? sysinfo.library_version : "?");

    retro_set_environment(&HarnessFrontend::EnvironmentCallback);
    retro_set_video_refresh(&HarnessFrontend::VideoRefresh);
    retro_set_audio_sample(&HarnessFrontend::AudioSample);
    retro_set_audio_sample_batch(&HarnessFrontend::AudioSampleBatch);
    retro_set_input_poll(&HarnessFrontend::InputPoll);
    retro_set_input_state(&HarnessFrontend::InputState);

    auto t0 = std::chrono::steady_clock::now();
    auto tstamp = [&](const char* stage) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "[harness boot +%lldms] %s\n", static_cast<long long>(ms), stage);
    };

    tstamp("retro_init begin");
    retro_init();
    // retro_init() just set the global log filter to Debug (see CitraLibRetro
    // ctor). Quiet it to Warning so the per-frame Audio.DSP/GPU debug flood
    // stops hammering stderr — that flood alone throttles the frame loop.
    Common::Log::SetGlobalFilter(Common::Log::Filter(Common::Log::Level::Warning));
    tstamp("retro_init end");

    std::vector<uint8_t> rom;
    retro_game_info game{};
    game.path = rom_path.c_str();
    if (sysinfo.need_fullpath) {
        game.data = nullptr;
        game.size = 0;
    } else {
        rom = HarnessBinaryFile::Read(rom_path);
        if (rom.empty()) {
            std::fprintf(stderr, "soh3d_harness: could not read ROM %s\n", rom_path.c_str());
            retro_deinit();
            return EXIT_FAILURE;
        }
        game.data = rom.data();
        game.size = rom.size();
    }

    // Both-sides texture-pack decision. MUST be after retro_init() (Settings
    // globals live in the core) and before retro_load_game() (Core::System::
    // Load latches CustomTexManager::FindCustomTextures).
    HarnessFrontend::SetupTexPack(rom_path);

    tstamp("retro_load_game begin");
    // Watchdog around retro_load_game too — this is the long CPU-heavy step
    // (LLE core init, kernel modules, filesystem setup) where hangs would
    // otherwise be invisible. Give it more slack than a runtime frame.
    alarm(120);
    bool loaded = retro_load_game(&game);
    alarm(0);
    tstamp("retro_load_game end");
    if (!loaded) {
        std::fprintf(stderr, "soh3d_harness: retro_load_game returned false\n");
        retro_deinit();
        return EXIT_FAILURE;
    }

    tstamp("vulkan bringup begin");
    alarm(120);
    const bool videoReady = HarnessFrontend::InitializeOracleVideo();
    alarm(0);
    tstamp("vulkan bringup end");
    if (!videoReady) {
        retro_deinit();
        return EXIT_FAILURE;
    }

    // Create the SDL window BEFORE spawning the worker so no SDL touch
    // ever happens off-main. All present/event-pump work stays here on
    // the main thread; the worker only reads/writes CPU pixel buffers.
    HarnessFrontend::EnsureWindow();

    std::printf("boot succeeded\n");
    std::fflush(stdout);

    // Kick the worker: it runs the REPL and every retro_run/RunFrame.
    // On close, we signal g_quit_requested and give it a grace window
    // to unwind, then _exit if it hasn't returned yet (e.g. it's mid
    // loadstate deserialization, which isn't interruptible).
    g_worker_thread = std::thread([]() {
        HarnessRepl::Run();
        // REPL ended (e.g. `quit` cmd or stdin EOF) — signal main to exit.
        HarnessFrontend::RequestQuit();
    });

    // Main SDL event loop. SDL_WaitEventTimeout yields the CPU when
    // there are no events, so we aren't burning a core polling. 16ms
    // gives ~60 Hz present cadence for the SBS window.
    const bool headless = HarnessFrontend::Headless();
    while (!HarnessFrontend::QuitRequested()) {
        if (!headless) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    std::fprintf(stderr, "harness: window closed, shutting down\n");
                    HarnessFrontend::RequestQuit();
                }
            }
            HarnessFrontend::PresentSideBySide();
            SDL_Delay(16);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // Grace period so the worker can bail out of its current frame loop
    // cleanly. loadstate/savestate aren't interruptible — they'll block
    // the worker for multi-seconds — so beyond the grace period we
    // force-exit rather than sit on join() forever.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::fflush(stdout);
    std::fflush(stderr);
    _exit(EXIT_SUCCESS);
}
