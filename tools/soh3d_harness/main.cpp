// soh3d_harness — headless libretro-frontend host that drives Azahar's
// libretro core in-process (no dlopen, no .so). Azahar's citra_libretro
// source files are linked directly into this executable (see the sibling
// harness.cmake), so the retro_* entry points are just normal C symbols.
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
//   playstate            -> ok 0x<ptr>           (or err "not populated")
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
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "libretro.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

// -----------------------------------------------------------------------------
// Frame watchdog. Arm alarm(kFrameWatchdogSecs) before every retro_run() /
// RunFrame() call; disarm after. If a single frame takes longer than that we
// treat the process as hung — SIGALRM fires and the handler prints a stderr
// banner + a C stack trace via backtrace(3), then _exit's so the shell / driver
// script gets a definitive kill instead of an indefinite stall.
// Async-signal-safety note: printf isn't strictly signal-safe, but for a
// debugging kill-switch on a hang we're fine — the process is about to die.
// -----------------------------------------------------------------------------
static constexpr int kFrameWatchdogSecs = 5;
static std::atomic<const char*> g_watchdog_where{nullptr};
static std::atomic<uint64_t>    g_watchdog_frame{0};

static void WatchdogHandler(int) {
    const char* where = g_watchdog_where.load();
    std::fprintf(stderr,
        "\n===== harness watchdog: frame stalled >%ds =====\n"
        "  where     : %s\n"
        "  frame idx : %llu\n"
        "  backtrace :\n",
        kFrameWatchdogSecs, where ? where : "(unknown)",
        static_cast<unsigned long long>(g_watchdog_frame.load()));
    void* bt[64];
    int n = backtrace(bt, 64);
    backtrace_symbols_fd(bt, n, fileno(stderr));
    std::fprintf(stderr, "===== forcing _exit =====\n");
    std::fflush(stderr);
    _exit(124);
}

static void InstallWatchdog() {
    struct sigaction sa{};
    sa.sa_handler = WatchdogHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, nullptr);
}

struct FrameWatchdog {
    explicit FrameWatchdog(const char* where) {
        g_watchdog_where.store(where);
        g_watchdog_frame.fetch_add(1);
        alarm(kFrameWatchdogSecs);
    }
    ~FrameWatchdog() { alarm(0); }
};

// ---------------------------------------------------------------------------
// Direct-harness capture globals defined in libultraship's SDL3 renderer
// (Shipwright/libultraship/src/fast/backends/gfx_sdl3.cpp). We fill in the
// buffer pointer + capacity, set gSoh3dCapturePending=1, and the very next
// FinishRender inside SoH3D downloads its color-fb 0 into that buffer as
// packed RGBA8, writing gSoh3dCaptureW/H, and clears the flag.
extern "C" {
extern uint8_t*     gSoh3dCaptureBuf;
extern uint32_t     gSoh3dCaptureCap;
extern uint32_t     gSoh3dCaptureW;
extern uint32_t     gSoh3dCaptureH;
extern volatile int gSoh3dCapturePending;
}

// Direct Azahar API — the harness is linked in-process, so we bypass the
// libretro memory-map path (which only exposes HEAP/LINEAR/VRAM, not the
// process .data section where gPlayState lives) and read/write through
// Memory::MemorySystem directly. This is the whole point of the embed-
// not-run direction: use Azahar's own emulator API, not IPC.
#include "core/core.h"
#include "core/memory.h"

// SoH3D game-engine entry points. Declared here as extern "C" instead of
// #including soh's global.h / OTRGlobals.h to keep this TU compilable
// as plain C++ against Azahar's headers — soh's headers assume the
// zelda3d compile settings and would leak N64 types (u16, PlayState,
// gSaveContext) that Azahar's namespace can't handle. All of these are
// linked from soh_lib (see harness.cmake).
extern "C" {
    // Route spdlog's default_logger to stderr before ANY SoH code logs.
    // Idempotent. Defined in libultraship/src/ship/Context.cpp.
    void Ship_EarlyLogToStderr(void);
    void GameConsole_Init(void);
    void InitOTR(int argc, char* argv[]);
    void DeinitOTR(void);
    void CrashHandler_PrintSohData(char*, size_t*);
    typedef void (*CrashHandlerCallback)(char*, size_t*);
    void CrashHandlerRegisterCallback(CrashHandlerCallback callback);
    void BootCommands_Init(void);
    void Heaps_Alloc(void);
    void Heaps_Free(void);
    void Main_Init(void* arg);
    void Main_Shutdown(void);
    void RunFrame(void);

    // SoH3D-side state readers (soh_state.cpp, compiled with soh_settings).
    int SohState_HasPlayState(void);
    int SohState_SceneNum(void);
    int SohState_RoomNum(void);
    int SohState_PlayerPos(float* px, float* py, float* pz,
                          short* rx, short* ry, short* rz);
    typedef void (*SohState_ActorSink)(void* user, int cat, int id, unsigned long addr,
                                       float px, float py, float pz,
                                       short rx, short ry, short rz);
    int SohState_WalkActors(SohState_ActorSink sink, void* user);
    int SohState_Warp(unsigned short entrance);
    int SohState_Lighting(unsigned char ambient[3],
                         signed char light1Dir[3], unsigned char light1Color[3],
                         signed char light2Dir[3], unsigned char light2Color[3],
                         unsigned char fogColor[3],
                         short* fogNear, short* fogFar,
                         unsigned char lightCtxAmbient[3],
                         unsigned char lightCtxFogColor[3],
                         short* lightCtxFogNear, short* lightCtxFogFar);
    int SohState_Camera(float* eyeX,  float* eyeY,  float* eyeZ,
                       float* atX,   float* atY,   float* atZ,
                       float* upX,   float* upY,   float* upZ,
                       float* fov,
                       short* roll,
                       int*   activeCamId);
    int SohState_ActorSkeleton(int cat, int listIndex,
                              short* jointsXYZ, int maxJoints,
                              int* outJointCount, int* outAnimFrame,
                              int* outMorphFrame);
}

namespace {

std::string g_system_dir;
std::string g_save_dir;

// Persistent held-input mask driven by the `input` REPL command. Bit N is
// asserted when the frontend is polled for RETRO_DEVICE_ID_JOYPAD_N.
uint32_t g_input_mask = 0;

// Diagnostic counters — every InputState call increments these so the
// harness can prove whether the emulator is actually polling us.
uint64_t g_input_poll_count = 0;
uint32_t g_input_poll_ids_seen = 0; // bit N set if joypad id N was ever queried

// ---------------------------------------------------------------------------
// SBS harness window
//
// The harness owns ONE SDL3 window. Neither engine draws into it directly:
//
//   - Azahar renders through its libretro core to CPU pixels delivered via
//     VideoRefresh (XRGB8888). We force citra_layout_option = "single_screen"
//     in the env callback so those frames contain only the TOP screen. We
//     stash the latest frame in g_az_buf.
//
//   - SoH3D runs with SOH_HEADLESS=1 so libultraship creates its own SDL
//     window HIDDEN. Its SDL3 GPU backend still renders into fb 0's color
//     texture normally; before each RunFrame we set gSoh3dCapturePending=1
//     pointing gSoh3dCaptureBuf at g_soh_buf, and FinishRender downloads
//     the frame into it as tightly-packed RGBA8.
//
// Once we have (at least one side's) fresh pixels, PresentSbs() gets the
// harness window's surface, blits Azahar to the LEFT half + SoH3D to the
// RIGHT half, and calls SDL_UpdateWindowSurface. That's the entire SBS
// compositor — no textures, no shaders, no render pipeline of our own.
// ---------------------------------------------------------------------------
// SoH3D bring-up flag. Declared here early so the capture / present
// helpers below can gate their SoH3D-side branches; assigned inside
// HandleSohBoot further down.
bool g_soh_booted = false;

SDL_Window* g_win = nullptr;
int         g_win_w = 1280;  // total SBS width; ½ per engine
int         g_win_h = 480;

// Set when the SDL window's close-button (or WM close) fires OR the REPL
// runs `quit`. The main-thread event loop watches for it and exits the
// process; the worker thread also checks between frames so it can bail
// out of long `run N` loops.
std::atomic<bool> g_quit_requested{false};

// Worker thread — runs the REPL + retro_run + RunFrame calls. Main thread
// stays in SDL land: creating the window, pumping events, presenting.
// This is the architecture change from "everything on main" — before, a
// long retro_run or loadstate blocked the SDL event loop entirely and
// the window went "Not Responding" until it returned, and closing it
// was effectively impossible.
std::thread g_worker_thread;

std::vector<uint8_t> g_az_buf;      // XRGB8888
uint32_t g_az_w = 0, g_az_h = 0;
size_t   g_az_pitch = 0;
bool     g_az_dirty = false;

std::vector<uint8_t> g_soh_buf;     // RGBA8 (gSoh3dCaptureBuf backing)
bool     g_soh_dirty = false;

// SOH3D_HARNESS_HEADLESS=1 → no SBS window, no SDL video init.
// Everything else (worker, REPL, retro_run, SoH child) still runs.
static bool HarnessHeadless() {
    const char* v = std::getenv("SOH3D_HARNESS_HEADLESS");
    return v && *v && v[0] != '0';
}

// Main-thread only. Called once from main() before the worker starts,
// then never again. Worker MUST NOT touch SDL window handles.
void EnsureHarnessWindow() {
    if (g_win) return;
    if (HarnessHeadless()) return;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "harness: SDL_InitSubSystem(VIDEO) failed: %s\n",
                     SDL_GetError());
        return;
    }
    g_win = SDL_CreateWindow("SoH3D harness — Azahar | SoH3D",
                             g_win_w, g_win_h, SDL_WINDOW_RESIZABLE);
    if (!g_win) {
        std::fprintf(stderr, "harness: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return;
    }
}

void RequestSohCapture() {
    if (!g_soh_booted) return;
    // We force a small render resolution in HandleSohBoot's config write
    // (Window.Width/Height = 320/240) so fb 0 stays tiny. RmlUi still
    // reports its own document size independently — 2000x1500 in practice
    // on this box. Reserve 24 MB (2400x2400x4) to comfortably cover both.
    constexpr size_t kCap = static_cast<size_t>(2400) * 2400 * 4;
    if (g_soh_buf.size() < kCap) g_soh_buf.resize(kCap);
    gSoh3dCaptureBuf     = g_soh_buf.data();
    gSoh3dCaptureCap     = static_cast<uint32_t>(g_soh_buf.size());
    gSoh3dCapturePending = 1;
}

// Main-thread only. Called from the main SDL event loop between event
// polls. The worker thread MUST NOT call this — SDL surface access is
// window-owner-thread only.
void PresentSbs() {
    if (!g_win) return;
    SDL_Surface* dst = SDL_GetWindowSurface(g_win);
    if (!dst) return;
    const int halfW = dst->w / 2;

    SDL_FillSurfaceRect(dst, nullptr, 0);

    // LEFT: Azahar top-screen frame (XRGB8888). Surface wraps g_az_buf.
    if (g_az_w && g_az_h && !g_az_buf.empty()) {
        SDL_Surface* src = SDL_CreateSurfaceFrom(
            static_cast<int>(g_az_w), static_cast<int>(g_az_h),
            SDL_PIXELFORMAT_XRGB8888, g_az_buf.data(),
            static_cast<int>(g_az_pitch));
        if (src) {
            SDL_Rect r{0, 0, halfW, dst->h};
            SDL_BlitSurfaceScaled(src, nullptr, dst, &r, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(src);
        }
    }
    // RIGHT: SoH3D frame (RGBA8 tightly packed by W). Surface wraps g_soh_buf.
    // Note: byte order in memory is (R,G,B,A). SDL_PIXELFORMAT_RGBA8888 is
    // a *packed* 32-bit format with R in high bits — on little-endian, that
    // means bytes in memory are (A,B,G,R), the WRONG interpretation for our
    // data (this was the "SoH pane renders black on-screen despite raw
    // bytes being nonzero" bug). SDL_PIXELFORMAT_ABGR8888 (equivalently
    // SDL_PIXELFORMAT_RGBA32) is the byte-order match on LE.
    if (gSoh3dCaptureW && gSoh3dCaptureH && !g_soh_buf.empty()) {
        const int w = static_cast<int>(gSoh3dCaptureW);
        const int h = static_cast<int>(gSoh3dCaptureH);
        SDL_Surface* src = SDL_CreateSurfaceFrom(
            w, h, SDL_PIXELFORMAT_ABGR8888, g_soh_buf.data(), w * 4);
        if (src) {
            SDL_Rect r{halfW, 0, dst->w - halfW, dst->h};
            SDL_BlitSurfaceScaled(src, nullptr, dst, &r, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(src);
        }
    }
    SDL_UpdateWindowSurface(g_win);
    // g_pump_thread does the actual event pump every 33ms; a redundant
    // pump here would race with it on the internal event queue. Leave
    // it to the thread.
    g_az_dirty = false;
    g_soh_dirty = false;
}

void CoreLog(retro_log_level level, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::fprintf(stderr, "[core:%d] ", static_cast<int>(level));
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
}

bool EnvironmentCallback(unsigned cmd, void* data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *static_cast<bool*>(data) = true;
        return true;

    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return true;

    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        auto* cb = static_cast<retro_log_callback*>(data);
        cb->log = &CoreLog;
        return true;
    }

    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *static_cast<const char**>(data) = g_system_dir.c_str();
        return true;

    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *static_cast<const char**>(data) = g_save_dir.c_str();
        return true;

    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        // Force the software renderer for headless operation — no window, no
        // HW context needed.
        auto* var = static_cast<retro_variable*>(data);
        if (var->key && std::strcmp(var->key, "citra_graphics_api") == 0) {
            var->value = "Software";
            return true;
        }
        // Only the 3DS top screen is useful for parity — bottom is UI, not
        // game state. "single_screen" restricts VideoRefresh output to
        // just the top screen at 400x240.
        if (var->key && std::strcmp(var->key, "citra_layout_option") == 0) {
            var->value = "single_screen";
            return true;
        }
        var->value = nullptr;
        return false;
    }

    default:
        return false;
    }
}

void VideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch) {
    if (!data || !width || !height) return;
    const size_t need = pitch * height;
    if (g_az_buf.size() < need) g_az_buf.resize(need);
    std::memcpy(g_az_buf.data(), data, need);
    g_az_w = width;
    g_az_h = height;
    g_az_pitch = pitch;
    g_az_dirty = true;
}
void AudioSample(int16_t, int16_t) {}
size_t AudioSampleBatch(const int16_t*, size_t frames) { return frames; }
void InputPoll() {}
int16_t InputState(unsigned /*port*/, unsigned device, unsigned /*index*/, unsigned id) {
    if (device != RETRO_DEVICE_JOYPAD) return 0;
    if (id >= 32) return 0;
    ++g_input_poll_count;
    g_input_poll_ids_seen |= (1u << id);
    return (g_input_mask >> id) & 1;
}

std::vector<uint8_t> SlurpFile(const std::string& path) {
    std::vector<uint8_t> data;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return data;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz > 0) {
        data.resize(static_cast<size_t>(sz));
        if (std::fread(data.data(), 1, data.size(), f) != data.size()) {
            data.clear();
        }
    }
    std::fclose(f);
    return data;
}

bool WriteWholeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

std::optional<uint64_t> ParseNum(const std::string& s) {
    if (s.empty()) return std::nullopt;
    char* end = nullptr;
    uint64_t v = std::strtoull(s.c_str(), &end, 0);
    if (end == s.c_str() || *end != '\0') return std::nullopt;
    return v;
}

void PrintErr(const char* what) { std::printf("err %s\n", what); }

void HandleRun(std::istringstream& toks) {
    std::string n_s;
    if (!(toks >> n_s)) { PrintErr("run: usage: run <N>"); return; }
    auto n = ParseNum(n_s);
    if (!n) { PrintErr("run: bad N"); return; }
    uint64_t done = 0;
    const bool trace = std::getenv("SOH3D_HARNESS_TRACE_FRAMES") != nullptr;
    auto t_last = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < *n; ++i) {
        if (g_quit_requested.load()) break;
        {
            FrameWatchdog wd("HandleRun/retro_run");
            retro_run();
        }
        ++done;
        if (trace && (done % 30) == 0) {
            auto now = std::chrono::steady_clock::now();
            auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now - t_last).count();
            std::fprintf(stderr, "[harness] run: %llu / %llu (last 30 frames = %lld ms)\n",
                         static_cast<unsigned long long>(done),
                         static_cast<unsigned long long>(*n),
                         static_cast<long long>(ms));
            t_last = now;
        }
    }
    std::printf("ok run %llu\n", static_cast<unsigned long long>(done));
}

void HandleRead(std::istringstream& toks, int width) {
    std::string va_s;
    if (!(toks >> va_s)) { PrintErr("read: usage: r<8|16|32> <va>"); return; }
    auto va = ParseNum(va_s);
    if (!va) { PrintErr("read: bad va"); return; }
    auto& mem = Core::System::GetInstance().Memory();
    // Read8/16 have no OrNullopt variant; fall back to ReadBlock via GetPointer.
    if (width == 32) {
        auto v = mem.Read32OrNullopt(static_cast<uint32_t>(*va));
        if (!v) { PrintErr("read: unmapped va"); return; }
        std::printf("ok 0x%08x\n", *v);
    } else {
        auto* p = mem.GetPointer(static_cast<uint32_t>(*va));
        if (!p) { PrintErr("read: unmapped va"); return; }
        if (width == 8) std::printf("ok 0x%02x\n", static_cast<unsigned>(*p));
        else {
            uint16_t v = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
            std::printf("ok 0x%04x\n", v);
        }
    }
}

void HandleWrite(std::istringstream& toks, int width) {
    std::string va_s, val_s;
    if (!(toks >> va_s >> val_s)) { PrintErr("write: usage: w<8|16|32> <va> <val>"); return; }
    auto va = ParseNum(va_s);
    auto val = ParseNum(val_s);
    if (!va || !val) { PrintErr("write: bad args"); return; }
    auto& mem = Core::System::GetInstance().Memory();
    switch (width) {
    case 8:  mem.Write8(static_cast<uint32_t>(*va),  static_cast<uint8_t>(*val));  break;
    case 16: mem.Write16(static_cast<uint32_t>(*va), static_cast<uint16_t>(*val)); break;
    case 32: mem.Write32(static_cast<uint32_t>(*va), static_cast<uint32_t>(*val)); break;
    }
    std::printf("ok\n");
}

void HandleMem(std::istringstream& toks) {
    std::string va_s, n_s;
    if (!(toks >> va_s >> n_s)) { PrintErr("mem: usage: mem <va> <n>"); return; }
    auto va = ParseNum(va_s);
    auto n  = ParseNum(n_s);
    if (!va || !n) { PrintErr("mem: bad args"); return; }
    if (*n > 4096) { PrintErr("mem: n too large (max 4096)"); return; }
    auto& mem = Core::System::GetInstance().Memory();
    auto* p = mem.GetPointer(static_cast<uint32_t>(*va));
    if (!p) { PrintErr("mem: unmapped va"); return; }
    std::printf("ok ");
    for (uint64_t i = 0; i < *n; ++i) std::printf("%02x", p[i]);
    std::printf("\n");
}

// Dump a contiguous VA range to a binary file. Backbone of any RE
// pass — dumping .data (RW-Private, ~830 KB on OoT3D) in one shot for
// offline analysis is way faster than 200+ `mem` REPL round-trips.
//
// Reads through Azahar's MemorySystem::GetPointer, which returns a
// raw pointer into the emulator's mapped-VA host memory. Unmapped
// pages produce a null GetPointer and we bail early rather than
// segfaulting on the memcpy.
void HandleDumpRange(std::istringstream& toks) {
    std::string va_s, n_s, path;
    if (!(toks >> va_s >> n_s >> path)) {
        PrintErr("dumprange: usage: dumprange <va> <size> <path>");
        return;
    }
    auto va = ParseNum(va_s);
    auto n  = ParseNum(n_s);
    if (!va || !n) { PrintErr("dumprange: bad args"); return; }
    if (*n == 0) { PrintErr("dumprange: size must be > 0"); return; }
    auto& mem = Core::System::GetInstance().Memory();
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { PrintErr("dumprange: open failed"); return; }
    // Chunk by 4KB (typical page boundary). Between chunks we
    // re-resolve GetPointer so a page boundary that crosses a
    // mapping change still reads correctly.
    const uint64_t chunk_sz = 4096;
    uint64_t written = 0, unmapped_bytes = 0;
    for (uint64_t off = 0; off < *n; off += chunk_sz) {
        const uint32_t cva = static_cast<uint32_t>(*va + off);
        const uint64_t rem = std::min(chunk_sz, *n - off);
        auto* p = mem.GetPointer(cva);
        if (!p) {
            // Unmapped — write zeros to keep offsets stable.
            std::vector<uint8_t> zeros(rem, 0);
            std::fwrite(zeros.data(), 1, rem, f);
            unmapped_bytes += rem;
        } else {
            std::fwrite(p, 1, rem, f);
        }
        written += rem;
    }
    std::fclose(f);
    std::printf("ok dumprange 0x%08x..0x%08x (%llu bytes, %llu unmapped) -> %s\n",
                (unsigned)*va, (unsigned)(*va + *n),
                (unsigned long long)written,
                (unsigned long long)unmapped_bytes, path.c_str());
}

void HandleLoadState(std::istringstream& toks) {
    std::string path;
    if (!(toks >> path)) { PrintErr("loadstate: usage: loadstate <path>"); return; }
    auto buf = SlurpFile(path);
    if (buf.empty()) { PrintErr("loadstate: read failed"); return; }
    if (!Core::System::GetInstance().LoadStateBuffer(std::move(buf))) {
        PrintErr("loadstate: core rejected buffer");
        return;
    }
    std::printf("ok\n");
}

// SoH3D bring-up as a REPL command so failure modes are isolated —
// call this from a test script instead of always booting SoH3D on
// startup and taking the whole harness down on any early crash.
void HandleSohBoot(std::istringstream&) {
    if (g_soh_booted) { PrintErr("soh_boot: already booted"); return; }
    // The harness owns the display; SoH3D must render to fb 0 but NOT open
    // a visible window of its own. SOH_HEADLESS=1 makes libultraship's
    // SDL3 backend create its window HIDDEN, and rendering into fb 0 (which
    // is what our capture path downloads) still happens normally.
    setenv("SOH_HEADLESS", "1", 1);
    setenv("SOH3D_HEADLESS", "1", 1);

    // Force a small render resolution. Without this, libultraship reads
    // Window.Width/Height from shipofharkinian.json — which on a HiDPI
    // display is the full monitor pixel size (e.g. 3840x1975 -> a
    // 9600x4938 fb 0 after internal scaling, ~190 MB / frame, enough to
    // OOM the GPU). 320x240 is more than the 3DS top-screen resolution
    // and plenty for parity work; keep it low for the harness.
    if (!std::filesystem::exists("shipofharkinian.json")) {
        std::FILE* f = std::fopen("shipofharkinian.json", "w");
        if (f) {
            std::fprintf(f,
                "{\n"
                "  \"Window\": { \"Width\": 320, \"Height\": 240 },\n"
                "  \"CVars\": { \"gInternalResolution\": 1.0 }\n"
                "}\n");
            std::fclose(f);
        }
    }
    EnsureHarnessWindow();
    static char argv0[] = "soh3d_harness";
    static char* argv[] = { argv0, nullptr };
    GameConsole_Init();
    InitOTR(1, argv);
    CrashHandlerRegisterCallback(&CrashHandler_PrintSohData);
    BootCommands_Init();
    Heaps_Alloc();
    Main_Init(nullptr);
    g_soh_booted = true;
    std::printf("ok soh_boot\n");
}

void HandleSohStep(std::istringstream& toks) {
    if (!g_soh_booted) { PrintErr("soh_step: run soh_boot first"); return; }
    std::string n_s;
    if (!(toks >> n_s)) { PrintErr("soh_step: usage: soh_step <N>"); return; }
    auto n = ParseNum(n_s);
    if (!n) { PrintErr("soh_step: bad N"); return; }
    uint64_t done = 0;
    for (uint64_t i = 0; i < *n; ++i) {
        if (g_quit_requested.load()) break;
        RequestSohCapture();
        FrameWatchdog wd("HandleSohStep/RunFrame");
        RunFrame();
        ++done;
    }
    std::printf("ok soh_step %llu\n", static_cast<unsigned long long>(done));
}

// The two-engine driver: advance BOTH engines one frame at a time, so
// they stay lockstep. Requires soh_boot first; without it, `step` is
// just an alias for `run` (Azahar-only).
void HandleStep(std::istringstream& toks) {
    std::string n_s;
    if (!(toks >> n_s)) { PrintErr("step: usage: step <N>"); return; }
    auto n = ParseNum(n_s);
    if (!n) { PrintErr("step: bad N"); return; }
    uint64_t done = 0;
    for (uint64_t i = 0; i < *n; ++i) {
        if (g_quit_requested.load()) break;
        { FrameWatchdog wd("HandleStep/retro_run"); retro_run(); }
        if (g_soh_booted) {
            RequestSohCapture();
            FrameWatchdog wd("HandleStep/RunFrame"); RunFrame();
        }
        ++done;
    }
    std::printf("ok step %llu %s\n",
                static_cast<unsigned long long>(done),
                g_soh_booted ? "azahar+soh3d" : "azahar-only");
}

void HandleDiag(std::istringstream&) {
    std::printf("ok mask=0x%08x polls=%llu ids_seen=0x%08x\n"
                "  az:  booted=1 w=%u h=%u pitch=%zu dirty=%d\n"
                "  soh: booted=%d captureW=%u captureH=%u pending=%d\n",
                g_input_mask,
                static_cast<unsigned long long>(g_input_poll_count),
                g_input_poll_ids_seen,
                g_az_w, g_az_h, g_az_pitch, g_az_dirty ? 1 : 0,
                g_soh_booted ? 1 : 0,
                gSoh3dCaptureW, gSoh3dCaptureH, gSoh3dCapturePending);
}

// Forward decls — Compare*Impl bodies live further down.
void CompareSceneImpl();
void ComparePlayerImpl();
void CompareActorsImpl();
void CompareCameraImpl();
void CompareSkeletonImpl(int cat, int listIndex);
void CompareLightingImpl();
void CompareTitleActorsImpl();

// -- Snapshot ---------------------------------------------------------------
//
// Dump both engines' latest captured framebuffers to <basepath>.az.ppm and
// <basepath>.soh.ppm. This is the primitive every sweep builds on: after
// the sweep drives both engines to a comparable state, snapshot() writes
// the visual state to disk so offline tools can diff pixels.
//
// Formats: g_az_buf is XRGB8888 with stride g_az_pitch. On little-endian
// that's byte order (B,G,R,X) per pixel. g_soh_buf is byte order (R,G,B,A)
// tightly packed w*4. PPM P6 is (R,G,B) big-endian byte order.
bool WriteAzahar_Ppm(const std::string& path) {
    if (!g_az_w || !g_az_h || g_az_buf.empty()) return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%u %u\n255\n", g_az_w, g_az_h);
    for (uint32_t y = 0; y < g_az_h; ++y) {
        const uint8_t* row = g_az_buf.data() + y * g_az_pitch;
        for (uint32_t x = 0; x < g_az_w; ++x) {
            const uint8_t* p = row + x * 4;      // B,G,R,X (XRGB8888 LE)
            uint8_t rgb[3] = { p[2], p[1], p[0] };
            std::fwrite(rgb, 1, 3, f);
        }
    }
    std::fclose(f);
    return true;
}

bool WriteSoh_Ppm(const std::string& path) {
    if (!gSoh3dCaptureW || !gSoh3dCaptureH || g_soh_buf.empty()) return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%u %u\n255\n", gSoh3dCaptureW, gSoh3dCaptureH);
    const uint32_t w = gSoh3dCaptureW, h = gSoh3dCaptureH;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* p = g_soh_buf.data() + (y * w + x) * 4; // R,G,B,A
            std::fwrite(p, 1, 3, f);
        }
    }
    std::fclose(f);
    return true;
}

void HandleSnapshot(std::istringstream& toks) {
    std::string base;
    if (!(toks >> base)) { PrintErr("snapshot: usage: snapshot <basepath>"); return; }
    const std::string az_path  = base + ".az.ppm";
    const std::string soh_path = base + ".soh.ppm";
    const bool az_ok  = WriteAzahar_Ppm(az_path);
    const bool soh_ok = WriteSoh_Ppm(soh_path);
    std::printf("ok snapshot\n"
                "  az:  %s %s (%ux%u)\n"
                "  soh: %s %s (%ux%u)\n",
                az_ok  ? "wrote" : "skip", az_path.c_str(),  g_az_w, g_az_h,
                soh_ok ? "wrote" : "skip", soh_path.c_str(),
                gSoh3dCaptureW, gSoh3dCaptureH);
}

// -- Sweep ------------------------------------------------------------------
//
// Automated parity driver. Sweep drives both engines to a shared state
// then emits structured comparison records to stdout for a consumer
// script (or an agent) to grep. Two modes so far:
//
//   sweep title <az_frames> <soh_frames> [basepath]
//     Bring both engines to the title screen (Azahar naturally boots
//     there; SoH3D via soh_boot if needed) and step each side to a
//     steady state. Optional basepath writes both fbs as PPM at the end.
//     This is the first-contact parity target — no Play state needed.
//
//   sweep entrances <az_frames> <soh_frames> <basepath> <ent1> <ent2> ...
//     For each entrance: force warp both engines, step both engines,
//     dump both compare subs + a snapshot at <basepath>.<ent>.{az,soh}.ppm.
//     Requires both engines already in Play; use a loadstate/boot_to_play
//     to get there first.
//
// Each record printed as one structured line for easy consumption:
//   sweep begin phase=<phase> step=<n>
//   sweep meta  key=val ...
//   sweep dim   compare=<sub>          then the compare block
//   sweep end   phase=<phase> ok=<0|1>
void SweepStepBoth(uint64_t az_n, uint64_t soh_n) {
    // Advance whichever side has the larger step count, or interleave
    // them for the overlap. Simplest lockstep-preserving order:
    const uint64_t lock = std::min(az_n, soh_n);
    for (uint64_t i = 0; i < lock; ++i) {
        if (g_quit_requested.load()) return;
        { FrameWatchdog wd("Sweep/retro_run"); retro_run(); }
        if (g_soh_booted) {
            RequestSohCapture();
            FrameWatchdog wd("Sweep/RunFrame"); RunFrame();
        }
    }
    for (uint64_t i = lock; i < az_n; ++i) {
        if (g_quit_requested.load()) return;
        FrameWatchdog wd("Sweep/retro_run"); retro_run();
    }
    for (uint64_t i = lock; i < soh_n; ++i) {
        if (g_quit_requested.load()) return;
        if (g_soh_booted) {
            RequestSohCapture();
            FrameWatchdog wd("Sweep/RunFrame"); RunFrame();
        }
    }
}

void SweepEmitCompares() {
    // Every compare that doesn't crash without a PlayState is safe here.
    // CompareScene / ComparePlayer / CompareActors / CompareLighting all
    // handle "no playstate" internally, so they work at title screen too
    // (they just print n/a for both sides).
    std::printf("sweep dim compare=scene\n");    CompareSceneImpl();
    std::printf("sweep dim compare=player\n");   ComparePlayerImpl();
    std::printf("sweep dim compare=actors\n");   CompareActorsImpl();
    std::printf("sweep dim compare=camera\n");   CompareCameraImpl();
    // Skeleton dump — Player only (cat 2 idx 0). The SoH struct layout
    // is known there; every other actor requires per-id RE that hasn't
    // been done. If Player isn't present (SoH still booting or scene
    // has no Link), CompareSkeletonImpl prints n/a — no crash.
    std::printf("sweep dim compare=skeleton cat=2 idx=0\n");
    CompareSkeletonImpl(2, 0);
    std::printf("sweep dim compare=lighting\n"); CompareLightingImpl();
}

void HandleSweepTitle(std::istringstream& toks) {
    std::string az_n_s, soh_n_s, base;
    if (!(toks >> az_n_s >> soh_n_s)) {
        PrintErr("sweep title: usage: sweep title <az_frames> <soh_frames> [basepath]");
        return;
    }
    toks >> base; // optional
    auto az_n  = ParseNum(az_n_s);
    auto soh_n = ParseNum(soh_n_s);
    if (!az_n || !soh_n) { PrintErr("sweep title: bad frame counts"); return; }

    std::printf("sweep begin phase=title az_frames=%llu soh_frames=%llu soh_booted=%d\n",
                (unsigned long long)*az_n, (unsigned long long)*soh_n,
                g_soh_booted ? 1 : 0);

    // Auto-boot SoH3D if not booted, so `sweep title` is a one-liner.
    if (!g_soh_booted) {
        std::istringstream empty("");
        HandleSohBoot(empty);
    }

    SweepStepBoth(*az_n, *soh_n);

    std::printf("sweep meta az=%ux%u soh=%ux%u soh_playstate=%d\n",
                g_az_w, g_az_h, gSoh3dCaptureW, gSoh3dCaptureH,
                g_soh_booted ? SohState_HasPlayState() : -1);

    SweepEmitCompares();

    bool snap_ok = true;
    if (!base.empty()) {
        const std::string az_p  = base + ".az.ppm";
        const std::string soh_p = base + ".soh.ppm";
        const bool az_ok  = WriteAzahar_Ppm(az_p);
        const bool soh_ok = WriteSoh_Ppm(soh_p);
        std::printf("sweep snapshot az=%s soh=%s az_path=%s soh_path=%s\n",
                    az_ok  ? "ok" : "skip", soh_ok ? "ok" : "skip",
                    az_p.c_str(), soh_p.c_str());
        snap_ok = az_ok && soh_ok;
    }
    std::printf("sweep end phase=title ok=%d\n", snap_ok ? 1 : 0);
    std::printf("ok sweep title\n");
}

void HandleSweep(std::istringstream& toks) {
    std::string sub;
    if (!(toks >> sub)) {
        PrintErr("sweep: usage: sweep <sub> — see `sweep list`");
        return;
    }
    if (sub == "list") {
        std::fprintf(stderr,
            "sweep subs:\n"
            "  title <az_frames> <soh_frames> [basepath]\n"
            "         Auto-boot SoH3D if needed, step both engines to a\n"
            "         steady title-screen state, emit compare records for\n"
            "         every dim, and (if basepath given) dump both fbs as\n"
            "         <basepath>.{az,soh}.ppm. Structured output lines\n"
            "         start with `sweep ` so consumers can grep them.\n"
            "\n"
            "Not yet implemented:\n"
            "  entrances <az_frames> <soh_frames> <basepath> <ent1> ...\n"
            "         Force-warp both engines to each entrance in turn,\n"
            "         step, compare, snapshot. Needs both engines already\n"
            "         in Play (Azahar boot-to-Play still TODO).\n");
        std::printf("ok sweep list\n");
        return;
    }
    if (sub == "title") { HandleSweepTitle(toks); return; }
    PrintErr(("sweep: unknown sub: " + sub).c_str());
}

void HandleInput(std::istringstream& toks) {
    std::string mask_s;
    if (!(toks >> mask_s)) { PrintErr("input: usage: input <mask>"); return; }
    auto m = ParseNum(mask_s);
    if (!m) { PrintErr("input: bad mask"); return; }
    g_input_mask = static_cast<uint32_t>(*m);
    std::printf("ok\n");
}

void HandleSaveState(std::istringstream& toks) {
    std::string path;
    if (!(toks >> path)) { PrintErr("savestate: usage: savestate <path>"); return; }
    auto buf = Core::System::GetInstance().SaveStateBuffer();
    if (!WriteWholeFile(path, buf)) { PrintErr("savestate: write failed"); return; }
    std::printf("ok\n");
}

// -- OoT3D RE knowledge lives here -------------------------------------------
//
// gPlayState (0x0050AF34, .data) -> PlayState* (0 until the game is in
// the Play gamestate, i.e. not in menus / logos / title). See
// tools/oracle_motion_sample.py and oot3d-decomp docs/actor_layout.md
// for the layout this walks.
//
// The transitionTrigger (u8 @ +0x5C2D, TRANS_TRIGGER_START = 20) and
// nextEntranceIndex (u16 @ +0x5C32) offsets come from prior OoT3D RE
// work (memory of a retired link_ctl.py); cross-checking them against
// oot3d-decomp when it comes back on this machine is a proper follow-up.
constexpr uint32_t GPLAYSTATE_VA           = 0x0050AF34;
constexpr uint32_t SCENENUM_OFF            = 0x0104;

// OoT3D title-demo state. GREZZO refactored the title/opening path on 3DS
// so it is NOT a Play gamestate — gPlayState @ 0x0050AF34 stays 0 during
// the whole title+demo loop. Instead the title context lives INLINE in
// .data at the SAME VA that gPlayState occupies during Play (overloaded
// slot: 0 in title mode = the inline struct's field +0x00; heap pointer
// in Play mode). RE'd from FUN_0046ac98 (title-state init) in code.bin.
constexpr uint32_t TITLE_CTX_VA            = 0x0050AF34;
constexpr uint32_t TITLE_SCENE_OFF         = 0x006C;  // *0x0050afa0 == 0x51 at title
constexpr uint32_t TITLE_ACTIVE_OFF        = 0x0078;  // *0x0050afac == 1 at title

// OoT3D title-demo SkelAnime limb-transform table. The writer chain is
// FUN_003204a4 (anim dispatcher, base=DAT_003208DC=0x005642D0, stride=0x24)
// → FUN_00347550 (keyframe evaluator: mask&1→pos, mask&2→rot, mask&4→scale).
// This is one statically-pre-allocated actor's live limb array — likely the
// title-demo Link or Epona; 25 entries = 25 limbs. Provenance +
// interpretation in oot3d-decomp/docs/title_gamestate.md.
constexpr uint32_t TITLE_POSE_TABLE_VA     = 0x005642D0;
constexpr uint32_t TITLE_POSE_COUNT        = 25;
constexpr uint32_t TITLE_POSE_STRIDE       = 36;
constexpr uint32_t ACTORCTX_OFF            = 0x208C;
constexpr uint32_t ACTOR_LISTS_OFF         = 0x000C;
constexpr uint32_t ACTOR_ID_OFF            = 0x0000;
constexpr uint32_t ACTOR_POS_OFF           = 0x0008;
constexpr uint32_t ACTOR_ROT_OFF           = 0x0014;
constexpr uint32_t ACTOR_NEXT_OFF          = 0x0130;
constexpr uint32_t TRANSITION_TRIGGER_OFF  = 0x5C2D;
constexpr uint32_t NEXT_ENTRANCE_OFF       = 0x5C32;
constexpr uint8_t  TRANS_TRIGGER_START     = 20;

std::optional<uint32_t> CurrentPlayState() {
    auto& mem = Core::System::GetInstance().Memory();
    auto v = mem.Read32OrNullopt(GPLAYSTATE_VA);
    if (!v || *v == 0) return std::nullopt;
    return *v;
}

// True when the emulator is in the title-demo (logo + Hyrule-field flyover),
// which is NOT a Play gamestate on 3DS but an inline .data-resident context.
// Detection: scene==0x51 at TITLE_SCENE_OFF and active flag set — the two
// discriminators FUN_0046ac98 sets during title init. Both must match; a
// stale post-Play read of the slot would fail either check.
bool TitleActive() {
    auto& mem = Core::System::GetInstance().Memory();
    auto scn = mem.Read32OrNullopt(TITLE_CTX_VA + TITLE_SCENE_OFF);
    auto act = mem.Read32OrNullopt(TITLE_CTX_VA + TITLE_ACTIVE_OFF);
    return scn && act && (*scn & 0xFFFF) == 0x51 && *act == 1;
}

void HandlePlayState(std::istringstream&) {
    auto ps = CurrentPlayState();
    if (!ps) { PrintErr("playstate: not populated (still in menu/title?)"); return; }
    std::printf("ok 0x%08x\n", *ps);
}

// Dump all 24 title-demo pose entries. Multiline reply, terminated by `ok end`.
// Only meaningful at title (TitleActive()); returns err otherwise.
void HandleTitleActors(std::istringstream&) {
    if (!TitleActive()) {
        PrintErr("titleactors: not at title (scene!=0x51 or active flag clear)");
        return;
    }
    auto& mem = Core::System::GetInstance().Memory();
    std::printf("ok titleactors %u\n", TITLE_POSE_COUNT);
    for (uint32_t i = 0; i < TITLE_POSE_COUNT; ++i) {
        const uint32_t va = TITLE_POSE_TABLE_VA + i * TITLE_POSE_STRIDE;
        float p[3], r[3], s[3];
        bool bad = false;
        for (int j = 0; j < 3; ++j) {
            auto vp = mem.Read32OrNullopt(va + 0  + j*4);
            auto vr = mem.Read32OrNullopt(va + 12 + j*4);
            auto vs = mem.Read32OrNullopt(va + 24 + j*4);
            if (!vp || !vr || !vs) { bad = true; break; }
            std::memcpy(&p[j], &*vp, 4);
            std::memcpy(&r[j], &*vr, 4);
            std::memcpy(&s[j], &*vs, 4);
        }
        if (bad) { std::printf("  %2u @ 0x%08x  <unmapped>\n", i, va); continue; }
        std::printf("  %2u @ 0x%08x  pos=(%9.2f,%9.2f,%9.2f)  rot=(%6.3f,%6.3f,%6.3f)  scale=(%.2f,%.2f,%.2f)\n",
                    i, va, p[0], p[1], p[2], r[0], r[1], r[2], s[0], s[1], s[2]);
    }
    std::printf("ok end\n");
}

void HandleScene(std::istringstream&) {
    auto ps = CurrentPlayState();
    if (!ps) { PrintErr("scene: no playstate"); return; }
    auto sn = Core::System::GetInstance().Memory().Read32OrNullopt(*ps + SCENENUM_OFF);
    if (!sn) { PrintErr("scene: sceneNum unmapped"); return; }
    std::printf("ok 0x%04x\n", static_cast<unsigned>(*sn & 0xFFFF));
}

void HandleWarp(std::istringstream& toks) {
    std::string ent_s;
    if (!(toks >> ent_s)) { PrintErr("warp: usage: warp <entrance>"); return; }
    auto ent = ParseNum(ent_s);
    if (!ent) { PrintErr("warp: bad entrance"); return; }
    auto ps = CurrentPlayState();
    if (!ps) { PrintErr("warp: no playstate — run frames or loadstate first"); return; }
    auto& mem = Core::System::GetInstance().Memory();
    mem.Write16(*ps + NEXT_ENTRANCE_OFF, static_cast<uint16_t>(*ent));
    mem.Write8(*ps + TRANSITION_TRIGGER_OFF, TRANS_TRIGGER_START);
    std::printf("ok warp 0x%04x\n", static_cast<unsigned>(*ent & 0xFFFF));
}

void HandleActors(std::istringstream&) {
    auto ps = CurrentPlayState();
    if (!ps) { PrintErr("actors: no playstate"); return; }
    auto& mem = Core::System::GetInstance().Memory();
    const uint32_t ctx = *ps + ACTORCTX_OFF;

    struct Entry { uint32_t cat; uint16_t id; uint32_t addr;
                   float pos[3]; int16_t rot[3]; };
    std::vector<Entry> entries;
    entries.reserve(64);

    for (uint32_t cat = 0; cat < 12; ++cat) {
        auto cnt = mem.Read32OrNullopt(ctx + ACTOR_LISTS_OFF + cat * 8 + 0);
        auto head = mem.Read32OrNullopt(ctx + ACTOR_LISTS_OFF + cat * 8 + 4);
        if (!cnt || !head) continue;
        uint32_t addr = *head;
        int32_t guard = static_cast<int32_t>(*cnt) + 4;
        while (addr != 0 && guard-- > 0) {
            auto id  = mem.Read32OrNullopt(addr + ACTOR_ID_OFF);
            auto px  = mem.Read32OrNullopt(addr + ACTOR_POS_OFF + 0);
            auto py  = mem.Read32OrNullopt(addr + ACTOR_POS_OFF + 4);
            auto pz  = mem.Read32OrNullopt(addr + ACTOR_POS_OFF + 8);
            auto rot = mem.Read32OrNullopt(addr + ACTOR_ROT_OFF + 0);
            auto rz  = mem.Read32OrNullopt(addr + ACTOR_ROT_OFF + 4);
            if (!id || !px || !py || !pz || !rot || !rz) break;
            Entry e{};
            e.cat  = cat;
            e.id   = static_cast<uint16_t>(*id & 0xFFFF);
            e.addr = addr;
            std::memcpy(&e.pos[0], &*px, 4);
            std::memcpy(&e.pos[1], &*py, 4);
            std::memcpy(&e.pos[2], &*pz, 4);
            e.rot[0] = static_cast<int16_t>(*rot & 0xFFFF);
            e.rot[1] = static_cast<int16_t>((*rot >> 16) & 0xFFFF);
            e.rot[2] = static_cast<int16_t>(*rz & 0xFFFF);
            entries.push_back(e);
            auto next = mem.Read32OrNullopt(addr + ACTOR_NEXT_OFF);
            if (!next) break;
            addr = *next;
        }
    }
    std::printf("ok actors %zu\n", entries.size());
    for (const auto& e : entries) {
        std::printf("  %u 0x%04x 0x%08x %.3f %.3f %.3f %d %d %d\n",
                    e.cat, e.id, e.addr,
                    e.pos[0], e.pos[1], e.pos[2],
                    e.rot[0], e.rot[1], e.rot[2]);
    }
    std::printf("ok end\n");
}

// ============================================================================
// compare <sub> — side-by-side dumps of matching state from both engines
//
// Each sub reports Azahar (OoT3D, the 3DS ground truth) on one line and
// SoH3D (the PC port under test) on the next, with the two prefixes
// `3ds:` and `soh:`. If either engine hasn't reached the Play gamestate
// yet (no PlayState populated), that side prints `n/a`.
//
// New subs go here — the dispatcher below just adds a name and a call.
// ============================================================================

void CompareSceneImpl() {
    // 3ds side
    auto ps = CurrentPlayState();
    if (ps) {
        auto& mem = Core::System::GetInstance().Memory();
        auto sn = mem.Read32OrNullopt(*ps + SCENENUM_OFF);
        std::printf("  3ds: sceneNum=0x%04x\n",
                    sn ? static_cast<unsigned>(*sn & 0xFFFF) : 0xFFFFu);
    } else if (TitleActive()) {
        // Title context: sceneNum is inline in .data — the SAME sentinel
        // that N64 OoT uses for its title-demo scene (SCENE_TESTROOM=0x51).
        std::printf("  3ds: sceneNum=0x0051 (title, inline ctx @ 0x%08x)\n",
                    TITLE_CTX_VA);
    } else {
        std::printf("  3ds: n/a (no playstate, no title)\n");
    }
    // soh side
    if (!SohState_HasPlayState()) {
        std::printf("  soh: n/a (no playstate)\n");
    } else {
        std::printf("  soh: sceneNum=0x%04x roomNum=%d\n",
                    static_cast<unsigned>(SohState_SceneNum() & 0xFFFF),
                    SohState_RoomNum());
    }
}

void ComparePlayerImpl() {
    // 3ds side: walk the current-scene actor table and find category=Player.
    auto ps = CurrentPlayState();
    if (!ps) {
        std::printf("  3ds: n/a (no playstate)\n");
    } else {
        auto& mem = Core::System::GetInstance().Memory();
        const uint32_t ctx = *ps + ACTORCTX_OFF;
        bool found = false;
        for (uint32_t cat = 0; cat < 12 && !found; ++cat) {
            auto head = mem.Read32OrNullopt(ctx + ACTOR_LISTS_OFF + cat * 8 + 4);
            if (!head || *head == 0) continue;
            auto id = mem.Read32OrNullopt(*head + ACTOR_ID_OFF);
            if (!id || (*id & 0xFFFF) != 0) continue; // Player = actor id 0
            auto px = mem.Read32OrNullopt(*head + ACTOR_POS_OFF + 0);
            auto py = mem.Read32OrNullopt(*head + ACTOR_POS_OFF + 4);
            auto pz = mem.Read32OrNullopt(*head + ACTOR_POS_OFF + 8);
            auto rot = mem.Read32OrNullopt(*head + ACTOR_ROT_OFF + 0);
            auto rz = mem.Read32OrNullopt(*head + ACTOR_ROT_OFF + 4);
            if (!px || !py || !pz || !rot || !rz) break;
            float fpx, fpy, fpz;
            std::memcpy(&fpx, &*px, 4);
            std::memcpy(&fpy, &*py, 4);
            std::memcpy(&fpz, &*pz, 4);
            std::printf("  3ds: pos=(%.2f,%.2f,%.2f) rot=(%d,%d,%d)\n",
                        fpx, fpy, fpz,
                        static_cast<int>(static_cast<int16_t>(*rot & 0xFFFF)),
                        static_cast<int>(static_cast<int16_t>((*rot >> 16) & 0xFFFF)),
                        static_cast<int>(static_cast<int16_t>(*rz & 0xFFFF)));
            found = true;
        }
        if (!found) std::printf("  3ds: n/a (no player actor live)\n");
    }
    // soh side
    if (!SohState_HasPlayState()) {
        std::printf("  soh: n/a (no playstate)\n");
    } else {
        float px, py, pz;
        short rx, ry, rz;
        if (SohState_PlayerPos(&px, &py, &pz, &rx, &ry, &rz)) {
            std::printf("  soh: pos=(%.2f,%.2f,%.2f) rot=(%d,%d,%d)\n",
                        px, py, pz, rx, ry, rz);
        } else {
            std::printf("  soh: n/a (no player actor live)\n");
        }
    }
}

struct SohActor {
    int cat; int id; unsigned long addr;
    float pos[3]; short rot[3];
};

void CollectSohActor(void* user, int cat, int id, unsigned long addr,
                     float px, float py, float pz,
                     short rx, short ry, short rz) {
    auto* v = static_cast<std::vector<SohActor>*>(user);
    v->push_back(SohActor{cat, id, addr, {px, py, pz}, {rx, ry, rz}});
}

void CompareActorsImpl() {
    // 3ds side
    struct Entry3ds { int cat; int id; unsigned addr; float pos[3]; short rot[3]; };
    std::vector<Entry3ds> a3;
    auto ps = CurrentPlayState();
    if (ps) {
        auto& mem = Core::System::GetInstance().Memory();
        const uint32_t ctx = *ps + ACTORCTX_OFF;
        for (uint32_t cat = 0; cat < 12; ++cat) {
            auto cnt = mem.Read32OrNullopt(ctx + ACTOR_LISTS_OFF + cat * 8 + 0);
            auto head = mem.Read32OrNullopt(ctx + ACTOR_LISTS_OFF + cat * 8 + 4);
            if (!cnt || !head) continue;
            uint32_t addr = *head;
            int32_t guard = static_cast<int32_t>(*cnt) + 4;
            while (addr != 0 && guard-- > 0) {
                auto id = mem.Read32OrNullopt(addr + ACTOR_ID_OFF);
                auto px = mem.Read32OrNullopt(addr + ACTOR_POS_OFF + 0);
                auto py = mem.Read32OrNullopt(addr + ACTOR_POS_OFF + 4);
                auto pz = mem.Read32OrNullopt(addr + ACTOR_POS_OFF + 8);
                auto rot = mem.Read32OrNullopt(addr + ACTOR_ROT_OFF + 0);
                auto rz = mem.Read32OrNullopt(addr + ACTOR_ROT_OFF + 4);
                if (!id || !px || !py || !pz || !rot || !rz) break;
                Entry3ds e{};
                e.cat = static_cast<int>(cat);
                e.id  = static_cast<int>(*id & 0xFFFF);
                e.addr = addr;
                std::memcpy(&e.pos[0], &*px, 4);
                std::memcpy(&e.pos[1], &*py, 4);
                std::memcpy(&e.pos[2], &*pz, 4);
                e.rot[0] = static_cast<int16_t>(*rot & 0xFFFF);
                e.rot[1] = static_cast<int16_t>((*rot >> 16) & 0xFFFF);
                e.rot[2] = static_cast<int16_t>(*rz & 0xFFFF);
                a3.push_back(e);
                auto next = mem.Read32OrNullopt(addr + ACTOR_NEXT_OFF);
                if (!next) break;
                addr = *next;
            }
        }
    }
    // soh side
    std::vector<SohActor> as;
    if (SohState_HasPlayState()) SohState_WalkActors(&CollectSohActor, &as);
    std::printf("  3ds: %zu actor(s)\n", a3.size());
    for (const auto& e : a3) {
        std::printf("       cat=%d id=0x%04x addr=0x%08x pos=(%.1f,%.1f,%.1f)\n",
                    e.cat, e.id, e.addr, e.pos[0], e.pos[1], e.pos[2]);
    }
    std::printf("  soh: %zu actor(s)\n", as.size());
    for (const auto& e : as) {
        std::printf("       cat=%d id=0x%04x addr=0x%016lx pos=(%.1f,%.1f,%.1f)\n",
                    e.cat, e.id, e.addr, e.pos[0], e.pos[1], e.pos[2]);
    }
}

void CompareCameraImpl() {
    // 3ds side: OoT3D's Camera pointer array within the 3DS PlayState is not
    // RE'd yet. gPlayState + activeCamId + cameraPtrs[] offsets need to be
    // decompiled; until then only the SoH side prints. When it lands, mirror
    // the SoH branch below reading from Azahar's Memory::MemorySystem.
    std::printf("  3ds: n/a (activeCamId/cameraPtrs offsets in OoT3D PlayState not RE'd yet)\n");
    if (!SohState_HasPlayState()) {
        std::printf("  soh: n/a (no playstate)\n");
        return;
    }
    float ex, ey, ez, ax, ay, az, ux, uy, uz, fov;
    short roll;
    int activeCamId;
    if (!SohState_Camera(&ex, &ey, &ez, &ax, &ay, &az, &ux, &uy, &uz,
                         &fov, &roll, &activeCamId)) {
        std::printf("  soh: n/a (no active camera)\n");
        return;
    }
    std::printf("  soh: camId=%d eye=(%.2f,%.2f,%.2f) at=(%.2f,%.2f,%.2f) up=(%.2f,%.2f,%.2f)\n"
                "       fov=%.2f roll=%d\n",
                activeCamId, ex, ey, ez, ax, ay, az, ux, uy, uz, fov, roll);
}

void CompareSkeletonImpl(int cat, int listIndex) {
    // 3ds side: needs Actor + SkelAnime offset RE. Same shape as the SoH
    // side once the offset lands — scan the actor for a SkelAnime signature
    // (limbCount+mode+jointTable) and dump the joint table.
    std::printf("  3ds: n/a (Actor SkelAnime offset in OoT3D not RE'd yet)\n");
    if (!SohState_HasPlayState()) {
        std::printf("  soh: n/a (no playstate)\n");
        return;
    }
    short joints[32 * 3] = {};
    int jointCount = 0, animFrame = 0, morphFrame = 0;
    int written = SohState_ActorSkeleton(cat, listIndex, joints, 32,
                                         &jointCount, &animFrame, &morphFrame);
    if (written < 0) {
        std::printf("  soh: n/a (actor at cat=%d idx=%d not present)\n", cat, listIndex);
        return;
    }
    if (written == 0) {
        std::printf("  soh: cat=%d idx=%d has no SkelAnime\n", cat, listIndex);
        return;
    }
    std::printf("  soh: cat=%d idx=%d limbs=%d animFrame=%d morphWeightBits=0x%08x\n",
                cat, listIndex, jointCount, animFrame, (unsigned)morphFrame);
    for (int j = 0; j < written; ++j) {
        std::printf("       joint[%d] = (%d, %d, %d)\n",
                    j, joints[j * 3 + 0], joints[j * 3 + 1], joints[j * 3 + 2]);
    }
}

void CompareLightingImpl() {
    // 3ds side: OoT3D's EnvironmentContext offset within the 3DS PlayState
    // is not RE'd yet (the actorCtx / sceneNum / transitionTrigger fields
    // in this harness came from prior work; envCtx did not). Once that
    // offset lands, add the read here mirroring the SoH branch below.
    std::printf("  3ds: n/a (envCtx offset in OoT3D PlayState not RE'd yet)\n");
    // soh side
    if (!SohState_HasPlayState()) {
        std::printf("  soh: n/a (no playstate)\n");
        return;
    }
    unsigned char ambient[3], l1c[3], l2c[3], fog[3], lcAmb[3], lcFog[3];
    signed char l1d[3], l2d[3];
    short fogNear, fogFar, lcFogNear, lcFogFar;
    if (!SohState_Lighting(ambient, l1d, l1c, l2d, l2c, fog, &fogNear, &fogFar,
                          lcAmb, lcFog, &lcFogNear, &lcFogFar)) {
        std::printf("  soh: n/a (SohState_Lighting failed)\n");
        return;
    }
    std::printf("  soh: envLightSettings\n"
                "       ambient=(%u,%u,%u) fog=(%u,%u,%u) fogNear=%d fogFar=%d\n"
                "       light1 dir=(%d,%d,%d) color=(%u,%u,%u)\n"
                "       light2 dir=(%d,%d,%d) color=(%u,%u,%u)\n",
                ambient[0], ambient[1], ambient[2],
                fog[0], fog[1], fog[2], fogNear, fogFar,
                l1d[0], l1d[1], l1d[2], l1c[0], l1c[1], l1c[2],
                l2d[0], l2d[1], l2d[2], l2c[0], l2c[1], l2c[2]);
    std::printf("  soh: lightCtx  ambient=(%u,%u,%u) fog=(%u,%u,%u) fogNear=%d fogFar=%d\n",
                lcAmb[0], lcAmb[1], lcAmb[2],
                lcFog[0], lcFog[1], lcFog[2], lcFogNear, lcFogFar);
}

// Compare titleactors: 3DS side reads the 25-entry SkelAnime pose table at
// TITLE_POSE_TABLE_VA, SoH3D side dumps the Player joint table if the demo
// Link actor is live. Both formats differ (3DS = 9 floats {pos,rot,scale};
// N64 = Vec3s joints), so the loop is closed structurally — a mapping
// between 3DS pose index K and N64 limb K is one visual alignment pass
// away, driven by matching per-limb per-frame motion.
void CompareTitleActorsImpl() {
    // 3ds side
    if (!TitleActive()) {
        std::printf("  3ds: n/a (not at title)\n");
    } else {
        auto& mem = Core::System::GetInstance().Memory();
        std::printf("  3ds: 25 poses @ 0x%08x  {Vec3 pos, Vec3 rot(rad), Vec3 scale}\n",
                    TITLE_POSE_TABLE_VA);
        for (uint32_t i = 0; i < TITLE_POSE_COUNT; ++i) {
            const uint32_t va = TITLE_POSE_TABLE_VA + i * TITLE_POSE_STRIDE;
            float p[3], r[3];
            bool bad = false;
            for (int j = 0; j < 3; ++j) {
                auto vp = mem.Read32OrNullopt(va + 0  + j*4);
                auto vr = mem.Read32OrNullopt(va + 12 + j*4);
                if (!vp || !vr) { bad = true; break; }
                std::memcpy(&p[j], &*vp, 4);
                std::memcpy(&r[j], &*vr, 4);
            }
            if (bad) continue;
            std::printf("       [%2u] pos=(%9.1f,%9.1f,%9.1f) rot=(%6.3f,%6.3f,%6.3f)\n",
                        i, p[0], p[1], p[2], r[0], r[1], r[2]);
        }
    }
    // soh side — walk actors, find Player, dump joint table
    if (!SohState_HasPlayState()) {
        std::printf("  soh: n/a (no playstate)\n");
        return;
    }
    short joints[32 * 3] = {};
    int jointCount = 0, animFrame = 0, morphFrame = 0;
    // Actor category 2 = ACTORCAT_PLAYER in OoT
    int n = SohState_ActorSkeleton(2, 0, joints, 32,
                                   &jointCount, &animFrame, &morphFrame);
    if (n < 0) {
        std::printf("  soh: n/a (no Player actor live at title)\n");
        return;
    }
    if (n == 0) {
        std::printf("  soh: Player has no SkelAnime\n");
        return;
    }
    std::printf("  soh: Player skelAnime  limbs=%d animFrame=%d morphWeight=0x%08x\n",
                jointCount, animFrame, (unsigned)morphFrame);
    for (int j = 0; j < n; ++j) {
        std::printf("       [%2d] jointVec3s=(%6d,%6d,%6d)\n",
                    j, joints[j * 3 + 0], joints[j * 3 + 1], joints[j * 3 + 2]);
    }
}

// ============================================================================
// force <sub> — write state into BOTH engines directly (no input driving)
//
// The point is to put both games into the same scripted starting state
// so a compare can inspect the delta right away. Every sub does the
// write on BOTH sides and reports which side accepted it.
//
// Current subs cover the "in-Play warp" case: both engines already
// booted through their title/intro sequences to the Play gamestate,
// and we tell each to trigger a scene transition to a shared entrance.
// The write uses the same `nextEntranceIndex + transitionTrigger`
// mechanism the game itself uses (see z_play.c:985).
//
// Forcing state OUTSIDE Play — e.g. "both engines at title screen" —
// needs a gamestate-machinery poke (SET_NEXT_GAMESTATE on the current
// gamestate's init/size fields). That requires locating each engine's
// current-gamestate pointer, which is TODO — the runFrameContext
// coroutine state in graph.c isn't exposed, and OoT3D's equivalent
// isn't RE'd yet.
// ============================================================================

void ForceWarpImpl(uint16_t entrance) {
    // 3ds side
    auto ps = CurrentPlayState();
    bool ok3ds = false;
    if (ps) {
        auto& mem = Core::System::GetInstance().Memory();
        mem.Write16(*ps + NEXT_ENTRANCE_OFF, entrance);
        mem.Write8(*ps + TRANSITION_TRIGGER_OFF, TRANS_TRIGGER_START);
        ok3ds = true;
    }
    std::printf("  3ds: %s\n", ok3ds ? "warp queued" : "n/a (no playstate)");
    // soh side
    bool okSoh = g_soh_booted && SohState_Warp(entrance);
    std::printf("  soh: %s\n", okSoh
                ? "warp queued"
                : (g_soh_booted ? "n/a (no playstate)" : "n/a (soh not booted)"));
}

void HandleForce(std::istringstream& toks) {
    std::string sub;
    if (!(toks >> sub)) {
        PrintErr("force: usage: force <sub> — see `force list`");
        return;
    }
    if (sub == "list") {
        std::fprintf(stderr,
            "force subs:\n"
            "  warp <entrance>   set nextEntranceIndex + transitionTrigger\n"
            "                    on BOTH engines; both games will process\n"
            "                    the scene transition on their next tick.\n"
            "                    Requires both engines to already be in\n"
            "                    the Play gamestate.\n"
            "\n"
            "Not yet implemented (needs gamestate-machinery RE):\n"
            "  gamestate <name>  jump both engines out of the current\n"
            "                    gamestate and into a named one (title,\n"
            "                    fileselect, opening, play) — useful for\n"
            "                    parity checks on non-Play screens.\n");
        std::printf("ok force list\n");
        return;
    }
    if (sub == "warp") {
        std::string ent_s;
        if (!(toks >> ent_s)) { PrintErr("force warp: usage: force warp <entrance>"); return; }
        auto ent = ParseNum(ent_s);
        if (!ent) { PrintErr("force warp: bad entrance"); return; }
        std::printf("force warp 0x%04x:\n", static_cast<unsigned>(*ent & 0xFFFF));
        ForceWarpImpl(static_cast<uint16_t>(*ent & 0xFFFF));
        std::printf("ok force warp 0x%04x\n", static_cast<unsigned>(*ent & 0xFFFF));
        return;
    }
    PrintErr(("force: unknown sub: " + sub).c_str());
}

void HandleCompare(std::istringstream& toks) {
    std::string sub;
    if (!(toks >> sub)) {
        PrintErr("compare: usage: compare <sub> — see `compare list`");
        return;
    }
    if (sub == "list") {
        std::fprintf(stderr,
            "compare subs:\n"
            "  scene              sceneNum (+ soh roomNum)\n"
            "  player             Link pos + rot\n"
            "  actors             full actor tables (cat, id, addr, pos)\n"
            "  camera             active camera eye/at/up/fov/roll — the\n"
            "                     title-screen demo drives this on a spline;\n"
            "                     mismatches here explain framing/parallax\n"
            "                     divergence before pixel comparison.\n"
            "  skeleton <cat> <i> joint table (SkelAnime) for the i-th actor\n"
            "                     in category <cat>. For pose parity: a\n"
            "                     mispose in Link's title demo shows here.\n"
            "  lighting           envLightSettings + lightCtx (ambient, dirs,\n"
            "                     light1/2 dir+color, fog, fog near/far).\n"
            "                     SoH3D runs its own renderer-side lighting\n"
            "                     but samples underlying scene values here,\n"
            "                     so mismatches here explain shading drift.\n");
        std::printf("ok compare list\n");
        return;
    }
    std::printf("compare %s:\n", sub.c_str());
    if      (sub == "scene")    CompareSceneImpl();
    else if (sub == "player")   ComparePlayerImpl();
    else if (sub == "actors")   CompareActorsImpl();
    else if (sub == "camera")   CompareCameraImpl();
    else if (sub == "skeleton") {
        std::string cat_s, idx_s;
        if (!(toks >> cat_s >> idx_s)) {
            PrintErr("compare skeleton: usage: compare skeleton <cat> <idx>");
            return;
        }
        auto pc = ParseNum(cat_s); auto pi = ParseNum(idx_s);
        if (!pc || !pi) { PrintErr("compare skeleton: bad cat/idx"); return; }
        CompareSkeletonImpl((int)*pc, (int)*pi);
    }
    else if (sub == "lighting") CompareLightingImpl();
    else if (sub == "titleactors") CompareTitleActorsImpl();
    else { PrintErr(("compare: unknown sub: " + sub).c_str()); return; }
    std::printf("ok compare %s\n", sub.c_str());
}

void PrintHelp() {
    std::fprintf(stderr,
        "soh3d_harness commands:\n"
        "  run <N>              advance N frames\n"
        "  r8|r16|r32 <va>      read u8/u16/u32 at VA (0x prefix ok)\n"
        "  w8|w16|w32 <va> <v>  write u8/u16/u32 at VA\n"
        "  mem <va> <n>         hex-dump N bytes (N<=4096)\n"
        "  input <mask>         set held button mask (RETRO_DEVICE_JOYPAD)\n"
        "                       B=0 Y=1 SELECT=2 START=3 UP=4 DOWN=5\n"
        "                       LEFT=6 RIGHT=7 A=8 X=9 L=10 R=11\n"
        "  loadstate <path>     load Azahar save state from file\n"
        "  savestate <path>     write Azahar save state to file\n"
        "  playstate            print gPlayState pointer\n"
        "  scene                print current sceneNum\n"
        "  warp <entrance>      write nextEntranceIndex + trigger=20\n"
        "  actors               dump current-scene actor table\n"
        "  soh_boot             bring up SoH3D (InitOTR/Heaps_Alloc/Main_Init)\n"
        "  soh_step <N>         advance SoH3D by N frames (RunFrame x N)\n"
        "  step <N>             advance BOTH engines in lockstep (Azahar\n"
        "                       retro_run + SoH3D RunFrame, per frame)\n"
        "  compare <sub>        side-by-side dump from both engines;\n"
        "                       `compare list` shows subs (scene/player/\n"
        "                       actors/lighting)\n"
        "  force <sub>          write state into BOTH engines (RE, no\n"
        "                       inputs); `force list` shows subs\n"
        "  snapshot <basepath>  write both fbs as <basepath>.{az,soh}.ppm\n"
        "  sweep <sub>          automated multi-step parity driver;\n"
        "                       `sweep list` shows subs (title, ...)\n"
        "  diag                 print harness diagnostics (input+capture)\n"
        "  quit                 exit\n"
        "  help                 this list\n");
    std::printf("ok\n");
}

void RunRepl() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (g_quit_requested.load()) {
            std::printf("ok quit (window closed)\n");
            std::fflush(stdout);
            return;
        }
        std::istringstream toks(line);
        std::string cmd;
        if (!(toks >> cmd) || cmd.empty()) continue;
        if      (cmd == "run")       HandleRun(toks);
        else if (cmd == "r8")        HandleRead(toks, 8);
        else if (cmd == "r16")       HandleRead(toks, 16);
        else if (cmd == "r32")       HandleRead(toks, 32);
        else if (cmd == "w8")        HandleWrite(toks, 8);
        else if (cmd == "w16")       HandleWrite(toks, 16);
        else if (cmd == "w32")       HandleWrite(toks, 32);
        else if (cmd == "mem")       HandleMem(toks);
        else if (cmd == "dumprange") HandleDumpRange(toks);
        else if (cmd == "input")     HandleInput(toks);
        else if (cmd == "diag")      HandleDiag(toks);
        else if (cmd == "loadstate") HandleLoadState(toks);
        else if (cmd == "savestate") HandleSaveState(toks);
        else if (cmd == "playstate") HandlePlayState(toks);
        else if (cmd == "titleactors") HandleTitleActors(toks);
        else if (cmd == "scene")     HandleScene(toks);
        else if (cmd == "warp")      HandleWarp(toks);
        else if (cmd == "actors")    HandleActors(toks);
        else if (cmd == "soh_boot")  HandleSohBoot(toks);
        else if (cmd == "soh_step")  HandleSohStep(toks);
        else if (cmd == "step")      HandleStep(toks);
        else if (cmd == "compare")   HandleCompare(toks);
        else if (cmd == "force")     HandleForce(toks);
        else if (cmd == "snapshot")  HandleSnapshot(toks);
        else if (cmd == "sweep")     HandleSweep(toks);
        else if (cmd == "help")      PrintHelp();
        else if (cmd == "quit") { std::printf("ok\n"); std::fflush(stdout); return; }
        else                         PrintErr(("unknown cmd: " + cmd).c_str());
        std::fflush(stdout);
    }
}

} // namespace

// Single-instance guard: leaked harness processes fight over the same SDL
// window / shipofharkinian.json / SoH captureBuf globals and produce output
// that looks plausible but is coming from the wrong PID. Enforce one at a
// time via an flock() on a per-user pidfile — deterministic, no PID race,
// dies with the process (kernel drops the lock on any exit path).
namespace {
int g_instance_lock_fd = -1;

bool AcquireSingletonLock() {
    const char* rundir = std::getenv("XDG_RUNTIME_DIR");
    std::string dir = rundir && *rundir ? std::string(rundir)
                                        : std::string(std::getenv("HOME") ?: "/tmp") + "/.cache";
    // Ensure the directory exists; mkdir failure is fine if already there.
    (void)mkdir(dir.c_str(), 0700);
    const std::string path = dir + "/soh3d_harness.lock";
    g_instance_lock_fd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (g_instance_lock_fd < 0) {
        std::fprintf(stderr,
                     "soh3d_harness: could not open lock %s: %s\n",
                     path.c_str(), std::strerror(errno));
        return false;
    }
    if (flock(g_instance_lock_fd, LOCK_EX | LOCK_NB) != 0) {
        // Read the holder PID from the file for a useful error.
        char buf[64] = {};
        ssize_t n = pread(g_instance_lock_fd, buf, sizeof(buf) - 1, 0);
        if (n < 0) n = 0;
        buf[n] = 0;
        std::fprintf(stderr,
            "soh3d_harness: another instance is already running (pid %s).\n"
            "  Kill it first: ~/.claude/skills/safe-kill/safekill soh3d_harness\n"
            "  Lock file: %s\n",
            buf[0] ? buf : "?", path.c_str());
        close(g_instance_lock_fd);
        g_instance_lock_fd = -1;
        return false;
    }
    // Write our pid for the next attempt's error message.
    if (ftruncate(g_instance_lock_fd, 0) == 0) {
        char pidbuf[32];
        int n = std::snprintf(pidbuf, sizeof(pidbuf), "%d\n", getpid());
        (void)write(g_instance_lock_fd, pidbuf, n);
    }
    return true;
}
} // namespace

int main(int argc, char** argv) {
    if (!AcquireSingletonLock()) return EXIT_FAILURE;
    InstallWatchdog();
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
        if (rom_path.empty()) rom_path = a;
    }
    if (rom_path.empty()) {
        if (const char* env = std::getenv("ZELDA3D_OOT3D_ROM"); env && *env) {
            rom_path = env;
        } else {
            std::fprintf(stderr, "soh3d_harness: no ROM (argv or $ZELDA3D_OOT3D_ROM)\n");
            return EXIT_FAILURE;
        }
    }

    g_system_dir = "scratch/harness/system";
    g_save_dir = "scratch/harness/save";

    std::fprintf(stderr, "soh3d_harness: rom = %s\n", rom_path.c_str());

    retro_system_info sysinfo{};
    retro_get_system_info(&sysinfo);
    std::fprintf(stderr, "soh3d_harness: core = %s %s\n",
                 sysinfo.library_name ? sysinfo.library_name : "?",
                 sysinfo.library_version ? sysinfo.library_version : "?");

    retro_set_environment(&EnvironmentCallback);
    retro_set_video_refresh(&VideoRefresh);
    retro_set_audio_sample(&AudioSample);
    retro_set_audio_sample_batch(&AudioSampleBatch);
    retro_set_input_poll(&InputPoll);
    retro_set_input_state(&InputState);

    auto t0 = std::chrono::steady_clock::now();
    auto tstamp = [&](const char* stage) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "[harness boot +%lldms] %s\n",
                     static_cast<long long>(ms), stage);
    };

    tstamp("retro_init begin");
    retro_init();
    tstamp("retro_init end");

    std::vector<uint8_t> rom;
    retro_game_info game{};
    game.path = rom_path.c_str();
    if (sysinfo.need_fullpath) {
        game.data = nullptr;
        game.size = 0;
    } else {
        rom = SlurpFile(rom_path);
        if (rom.empty()) {
            std::fprintf(stderr, "soh3d_harness: could not read ROM %s\n", rom_path.c_str());
            retro_deinit();
            return EXIT_FAILURE;
        }
        game.data = rom.data();
        game.size = rom.size();
    }

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

    // Create the SDL window BEFORE spawning the worker so no SDL touch
    // ever happens off-main. All present/event-pump work stays here on
    // the main thread; the worker only reads/writes CPU pixel buffers.
    EnsureHarnessWindow();

    std::printf("boot succeeded\n");
    std::fflush(stdout);

    // Kick the worker: it runs the REPL and every retro_run/RunFrame.
    // On close, we signal g_quit_requested and give it a grace window
    // to unwind, then _exit if it hasn't returned yet (e.g. it's mid
    // loadstate deserialization, which isn't interruptible).
    g_worker_thread = std::thread([]() {
        RunRepl();
        // REPL ended (e.g. `quit` cmd or stdin EOF) — signal main to exit.
        g_quit_requested.store(true);
    });

    // Main SDL event loop. SDL_WaitEventTimeout yields the CPU when
    // there are no events, so we aren't burning a core polling. 16ms
    // gives ~60 Hz present cadence for the SBS window.
    const bool headless = HarnessHeadless();
    while (!g_quit_requested.load()) {
        if (!headless) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_EVENT_QUIT ||
                    ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    std::fprintf(stderr, "harness: window closed, shutting down\n");
                    g_quit_requested.store(true);
                }
            }
            PresentSbs();
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
