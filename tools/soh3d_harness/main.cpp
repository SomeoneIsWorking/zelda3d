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
#include <cmath>
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
// zelda3d_cutscene.cpp — title-cs frame cursor (parity A/B)
int  Zelda3D_TitleCsFrame(void);
void Zelda3D_TitleCsSetFrame(int frame);
int  Zelda3D_TitleCsEndFrame(void);

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
    int SohState_CsFrames(void);
    int SohState_SetCsFrames(int frames);
    int SohState_PlayerPos(float* px, float* py, float* pz,
                          short* rx, short* ry, short* rz);
    typedef void (*SohState_ActorSink)(void* user, int cat, int id, unsigned long addr,
                                       float px, float py, float pz,
                                       short rx, short ry, short rz);
    int SohState_WalkActors(SohState_ActorSink sink, void* user);
    int SohState_Warp(unsigned short entrance);
    int SohState_ActorParamsAt(int cat, int index);
    int SohState_ActorListLen(int cat);
    int SohState_ActorInfoAt(int cat, int index,
                              int* out_id, int* out_params, unsigned int* out_flags,
                              float* out_px, float* out_py, float* out_pz,
                              short* out_rx, short* out_ry, short* out_rz);
    int SohState_SetInput(unsigned int button, int stickX, int stickY);
    int SohState_PlayerWallInfo(unsigned int* out_bgFlags,
                                 int* out_wallYaw, int* out_wallBgId,
                                 unsigned long* out_wallPoly,
                                 float* out_speedXZ, float* out_velY);
    int SohState_TeleportPlayer(float x, float y, float z);
    int SohState_SetPlayerYaw(int yaw_s16);
    int SohState_SetLinkAge(int age);
    int SohState_GetLinkAge(void);
    int SohState_SetEnvSlot(unsigned char slot);

    // watchhook.cpp — write-hook API on top of Azahar's RegisterWatchpoint.
    struct WatchRecord {
        uint32_t vaddr;
        uint32_t size;
        uint64_t data;
        uint32_t arm_pc;
        uint32_t arm_lr;
        uint64_t cycles;
        uint32_t arm_r0;
        uint32_t arm_r1;
        uint32_t arm_r2;
        uint32_t arm_r3;
        uint32_t arm_sp;
        uint32_t stack_words[256];
    };
    struct WatchRange {
        uint32_t addr;
        uint32_t size;
    };
    void Soh3d_WatchAddRange(uint32_t addr, uint32_t size);
    void Soh3d_WatchRemoveRange(uint32_t addr, uint32_t size);
    std::size_t Soh3d_WatchGetHits(uint32_t addr, WatchRecord* out,
                                    std::size_t max_out);
    void Soh3d_WatchClear(uint32_t addr);
    std::size_t Soh3d_WatchListRanges(WatchRange* out, std::size_t max_out);
    bool Soh3d_WatchGetLatestMatching(uint32_t range_base, uint64_t mask,
                                       uint64_t expected, WatchRecord* out);
    bool Soh3d_WatchIsRegistered(uint32_t addr);
    int SohState_DumpControlFlags(unsigned int* out_stateFlags1,
                                   int* out_csState, unsigned int* out_csIndex,
                                   unsigned int* out_nextCsIndex,
                                   int* out_transTrigger, int* out_csAction);
    int SohState_Lighting(unsigned char ambient[3],
                         signed char light1Dir[3], unsigned char light1Color[3],
                         signed char light2Dir[3], unsigned char light2Color[3],
                         unsigned char fogColor[3],
                         short* fogNear, short* fogFar,
                         unsigned char lightCtxAmbient[3],
                         unsigned char lightCtxFogColor[3],
                         short* lightCtxFogNear, short* lightCtxFogFar,
                         unsigned char* outUnkBF, unsigned char* outUnkBD,
                         float* outUnkD8);
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
    int SohState_ShrinkWindowVal(void);
    int SohState_Zelda3DLive(float* amb, float* l1col, float* l2col);
    int SohState_DayTimeAndEnv(unsigned int* daytime,
                              unsigned char* skybox1Idx, unsigned char* skybox2Idx,
                              float* skyboxBlend,
                              unsigned char* liveAmbient,
                              unsigned char* liveFogColor,
                              short* liveFogNear, short* liveFogFar);
    // TEMPORARY (item A, #146 moon-scale derivation) — see soh_state.cpp.
    int SohState_MoonDebug(float* sunPosY, float* color, float* scale, float* discScale);
}

extern "C" {
    extern char soh3d_draw_log_path[256];
    extern int  soh3d_draw_log_active;
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
// Az analog-stick override in libretro s16 range [-32768, 32767]. Written
// by the `analog <lx> <ly> [rx] [ry]` REPL cmd. Independent from
// g_input_mask (which is joypad bits only).
int16_t g_az_analog_lx = 0, g_az_analog_ly = 0, g_az_analog_rx = 0, g_az_analog_ry = 0;

int16_t InputState(unsigned /*port*/, unsigned device, unsigned index, unsigned id) {
    if (device == RETRO_DEVICE_ANALOG) {
        // index: RETRO_DEVICE_INDEX_ANALOG_LEFT=0, _RIGHT=1
        // id:    RETRO_DEVICE_ID_ANALOG_X=0, _Y=1
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT) {
            if (id == RETRO_DEVICE_ID_ANALOG_X) return g_az_analog_lx;
            if (id == RETRO_DEVICE_ID_ANALOG_Y) return g_az_analog_ly;
        } else if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
            if (id == RETRO_DEVICE_ID_ANALOG_X) return g_az_analog_rx;
            if (id == RETRO_DEVICE_ID_ANALOG_Y) return g_az_analog_ry;
        }
        return 0;
    }
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

// Scan a guest VA range for a byte pattern. Walks page-by-page via
// MemorySystem::GetPointer (skipping unmapped pages), handling matches that
// straddle two contiguously-mapped pages. Prints up to 32 hit VAs.
// Usage: memscan <va_start> <va_end> <hexpattern>
void HandleMemScan(std::istringstream& toks) {
    std::string s_s, e_s, pat_s;
    if (!(toks >> s_s >> e_s >> pat_s)) {
        PrintErr("memscan: usage: memscan <va_start> <va_end> <hexpattern>");
        return;
    }
    auto s = ParseNum(s_s);
    auto e = ParseNum(e_s);
    if (!s || !e || *e <= *s || (pat_s.size() % 2) || pat_s.size() < 8) {
        PrintErr("memscan: bad args (pattern >= 4 bytes hex)");
        return;
    }
    std::vector<uint8_t> pat(pat_s.size() / 2);
    for (size_t i = 0; i < pat.size(); ++i)
        pat[i] = static_cast<uint8_t>(std::stoul(pat_s.substr(i * 2, 2), nullptr, 16));
    auto& mem = Core::System::GetInstance().Memory();
    constexpr uint32_t kPage = 0x1000;
    std::vector<uint32_t> hits;
    std::vector<uint8_t> buf;      // rolling contiguous run
    uint32_t run_start = 0;
    auto flush = [&]() {
        if (buf.size() >= pat.size()) {
            for (size_t i = 0; i + pat.size() <= buf.size(); ++i) {
                if (std::memcmp(buf.data() + i, pat.data(), pat.size()) == 0) {
                    hits.push_back(run_start + static_cast<uint32_t>(i));
                    if (hits.size() >= 32) return;
                }
            }
        }
        buf.clear();
    };
    for (uint64_t page = *s & ~static_cast<uint64_t>(kPage - 1);
         page < *e && hits.size() < 32; page += kPage) {
        auto* p = mem.GetPointer(static_cast<uint32_t>(page));
        if (!p) { flush(); if (hits.size() >= 32) break; continue; }
        if (buf.empty()) run_start = static_cast<uint32_t>(page);
        buf.insert(buf.end(), p, p + kPage);
        if (buf.size() > (1u << 22)) {  // cap run buffer at 4MB, keep overlap
            for (size_t i = 0; i + pat.size() <= buf.size(); ++i)
                if (std::memcmp(buf.data() + i, pat.data(), pat.size()) == 0) {
                    hits.push_back(run_start + static_cast<uint32_t>(i));
                    if (hits.size() >= 32) break;
                }
            size_t keep = pat.size() - 1;
            run_start += static_cast<uint32_t>(buf.size() - keep);
            std::vector<uint8_t> tail(buf.end() - keep, buf.end());
            buf.swap(tail);
        }
    }
    flush();
    std::printf("ok memscan %zu", hits.size());
    for (auto h : hits) std::printf(" 0x%08x", h);
    std::printf("\n");
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
// Dump a contiguous PHYSICAL-address range to a file. Same as dumprange but reads via
// MemorySystem::GetPhysicalPointer — the accessor Az's SW rasterizer uses to reach texture
// data at FCRAM/VRAM physical addresses (0x18xxxxxx / 0x20xxxxxx) which don't appear in the
// virtual mapping GetPointer walks. Task-#16 atmospheric-overlay RE: the two title-demo
// full-screen textures live at physical 0x2095aa00 / 0x2091a900 and dumprange (virtual)
// reports the whole range unmapped.
void HandleDumpPhys(std::istringstream& toks) {
    std::string pa_s, n_s, path;
    if (!(toks >> pa_s >> n_s >> path)) {
        PrintErr("dumpphys: usage: dumpphys <phys_addr> <size> <path>");
        return;
    }
    auto pa = ParseNum(pa_s);
    auto n  = ParseNum(n_s);
    if (!pa || !n || *n == 0) { PrintErr("dumpphys: bad args"); return; }
    auto& mem = Core::System::GetInstance().Memory();
    auto* p = mem.GetPhysicalPointer(static_cast<uint32_t>(*pa));
    if (!p) { PrintErr("dumpphys: physical addr not mapped"); return; }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { PrintErr("dumpphys: open failed"); return; }
    std::fwrite(p, 1, *n, f);
    std::fclose(f);
    std::printf("ok dumpphys 0x%08x..0x%08x (%llu bytes) -> %s\n",
                (unsigned)*pa, (unsigned)(*pa + *n), (unsigned long long)*n, path.c_str());
}

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

    // Match Az's 3DS top-screen native render resolution 400x240 so
    // side-by-side captures are like-for-like (no upscale/downscale
    // filtering in the diff). Written UNCONDITIONALLY to defeat any
    // stale HiDPI-scaled window dims baked into a prior session's
    // shipofharkinian.json.
    {
        std::FILE* f = std::fopen("shipofharkinian.json", "w");
        if (f) {
            std::fprintf(f,
                "{\n"
                "  \"Window\": { \"Width\": 400, \"Height\": 240 },\n"
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

extern "C" volatile int      gSoh3dFb0LastCaptureAttempt;
extern "C" volatile uint32_t gSoh3dFb0LastW;
extern "C" volatile uint32_t gSoh3dFb0LastH;
extern "C" volatile int      gSoh3dFb0LastHasColor;
extern "C" volatile int      gSoh3dFb0LastInRange;

void HandleDiag(std::istringstream&) {
    std::printf("ok mask=0x%08x polls=%llu ids_seen=0x%08x\n"
                "  az:  booted=1 w=%u h=%u pitch=%zu dirty=%d\n"
                "  soh: booted=%d captureW=%u captureH=%u pending=%d\n"
                "  fb0: attempts=%d inRange=%d hasColor=%d lastW=%u lastH=%u\n"
                "ok end\n",
                g_input_mask,
                static_cast<unsigned long long>(g_input_poll_count),
                g_input_poll_ids_seen,
                g_az_w, g_az_h, g_az_pitch, g_az_dirty ? 1 : 0,
                g_soh_booted ? 1 : 0,
                gSoh3dCaptureW, gSoh3dCaptureH, gSoh3dCapturePending,
                (int)gSoh3dFb0LastCaptureAttempt,
                (int)gSoh3dFb0LastInRange, (int)gSoh3dFb0LastHasColor,
                (unsigned)gSoh3dFb0LastW, (unsigned)gSoh3dFb0LastH);
}

// Forward decls — Compare*Impl bodies live further down.
void CompareSceneImpl();
void ComparePlayerImpl();
void CompareActorsImpl();
void CompareCameraImpl();
void CompareSkeletonImpl(int cat, int listIndex);
void CompareLightingImpl();
void CompareTitleActorsImpl();
void CompareFirstDivImpl();

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
                "  soh: %s %s (%ux%u)\n"
                "ok end\n",
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

// gSaveContext (fixed .bss global, confirmed by content — oot3d-decomp
// docs/ram_map.md:56) @ 0x00587958. dayTime is a u16 @ +0x0C (+0x0A is a
// u16-aligned pad per the doc's "0x0C" byte offset; read the containing
// word and shift). This is a GLOBAL, not play-relative, so it is valid to
// read even during the title/opening-demo GameState (gPlayState==0) where
// the normal Play struct doesn't exist yet.
constexpr uint32_t GSAVECONTEXT_VA         = 0x00587958;
constexpr uint32_t SAVECONTEXT_DAYTIME_OFF = 0x0C;

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
constexpr uint32_t TITLE_POSE_TABLE_VA     = 0x005642D0;  // Table A: Epona
constexpr uint32_t TITLE_POSE_COUNT        = 25;
constexpr uint32_t TITLE_POSE_STRIDE       = 36;

// Table B: a second statically-pre-allocated title-demo actor. Writer is
// FUN_002bd9ec (DAT_002bdd38 = 0x005A54D8, stride 0x24), same shape as
// table A. Also 25 entries — the sibling actor in the demo (Link or a
// second horse). Runtime pos/rot data lands on separate values from
// table A, so the two are independent live pose streams.
constexpr uint32_t TITLE_POSE_TABLE_B_VA   = 0x005A54D8;
constexpr uint32_t TITLE_POSE_B_COUNT      = 25;

// OoT3D title-demo camera basis: at 0x005BE6D4.
// CORRECTED layout (JIT-caught writer FUN_004235B8 @ 0x004235d4 stores an
// inverted view matrix — see oot3d-decomp/docs/title_view_matrix_lh.md):
//   +0x00  eye     (Vec3f)
//   +0x0C  right   (Vec3f, unit; = forward × up, RH-shape)
//   +0x18  up      (Vec3f, unit)
//   +0x24  forward (Vec3f, unit; = at − eye normalised — the actual view dir)
// The dir[] field name below preserves what the SoH port sees at
// kZelda3dTitleAt-eye — the actual view forward at +0x24 — so parity
// metrics compare like-for-like against SoH's viewing direction.
constexpr uint32_t TITLE_CAM_BASIS_VA      = 0x005BE6D4;
struct Az3dsTitleCameraBasis {
    float eye[3];
    float dir[3];   // view forward — read from +0x24, NOT +0x0C
    float up[3];
};

// Typed accessor for the OoT3D title-camera basis. Returns true on success.
static bool Az_ReadTitleCameraBasis(Az3dsTitleCameraBasis* out) {
    auto& mem = Core::System::GetInstance().Memory();
    for (int j = 0; j < 3; ++j) {
        auto e = mem.Read32OrNullopt(TITLE_CAM_BASIS_VA + 0    + j*4);
        auto u = mem.Read32OrNullopt(TITLE_CAM_BASIS_VA + 0x18 + j*4);
        auto d = mem.Read32OrNullopt(TITLE_CAM_BASIS_VA + 0x24 + j*4);  // forward
        if (!e || !d || !u) return false;
        std::memcpy(&out->eye[j], &*e, 4);
        std::memcpy(&out->dir[j], &*d, 4);
        std::memcpy(&out->up [j], &*u, 4);
    }
    return true;
}

// OoT3D title-demo rider (Epona-carrying-Link) WORLD POSITION: single Vec3f
// at 0x005AFFB0. RE'd via a 4-byte-aligned Vec3f scan across three time
// deltas on gamestate_re/data_dt_{A,B60,B300}.bin — the only slot outside
// the two known pose tables and camera basis that held world-scale coords
// in the Hyrule Field range, moved at gallop speed (~3.9 units/frame), and
// stayed directionally consistent (cos(Δ60,Δ300)=0.999). See oot3d-decomp
// docs/title_actor_world_pos.md for the full derivation. LANDED in
// oot3d-decomp z_title_demo.c/.h as TitleDemo_GetRiderWorldPos().
//
// This slot serves as Link's effective world position at title (Link is
// drawn attached via Epona's SkelAnime; no second world-pos slot surfaced
// in the same scan). Used by CompareFirstDivImpl's title-actor block to
// compute a real |Δpos| metric against SoH's Player.world.pos, replacing
// the diagnostic-only limb-local dump.
constexpr uint32_t TITLE_LINK_WORLD_POS_VA = 0x005AFFB0;
struct Az3dsTitleLinkWorldPos {
    float pos[3];
};

// Typed accessor for the OoT3D title-demo rider world position. Returns
// true on success. The read is 12 contiguous bytes at
// TITLE_LINK_WORLD_POS_VA; any unmapped byte aborts and returns false so
// the caller falls through to "unmapped" rather than reporting garbage.
static bool Az_ReadTitleLinkPos(Az3dsTitleLinkWorldPos* out) {
    auto& mem = Core::System::GetInstance().Memory();
    for (int j = 0; j < 3; ++j) {
        auto v = mem.Read32OrNullopt(TITLE_LINK_WORLD_POS_VA + j*4);
        if (!v) return false;
        std::memcpy(&out->pos[j], &*v, 4);
    }
    return true;
}
constexpr uint32_t ACTORCTX_OFF            = 0x208C;
constexpr uint32_t ACTOR_LISTS_OFF         = 0x000C;
constexpr uint32_t ACTOR_ID_OFF            = 0x0000;
constexpr uint32_t ACTOR_POS_OFF           = 0x0008;
constexpr uint32_t ACTOR_ROT_OFF           = 0x0014;
constexpr uint32_t ACTOR_NEXT_OFF          = 0x0130;
// Player.speedXZ inside the OoT3D Actor struct. Discovered by a live
// memory scan under scripted walk-forward (scratch/az_speedxz_scan.py,
// 2026-07-03): 6 successive game-frame snapshots showed one offset with
// the acceleration signature 0.0 → 0.0 → 1.33 → 2.67 → 4.00 → 5.00 —
// classic Player speedXZ ramp saturating at the walking-speed cap 5.0.
// Same byte offset as SoH's N64 Actor.speedXZ (z64actor.h:227). 0x06c
// held the same value; may be a paired duplicate or an adjacent
// horizontal-speed field. Only 0x068 is confirmed as speedXZ.
constexpr uint32_t ACTOR_SPEEDXZ_OFF       = 0x0068;

// Player live-facing yaw. On N64, Actor.world.rot.y (at 0x14+2) tracks
// the actor's physical facing direction and Player_UpdateCommon writes
// it on every yaw update. On 3DS GREZZO decoupled these: the s16 at
// Actor+0x14+2 stores the initial spawn rotation and stays STATIC
// through gameplay, while the live "current facing" yaw is at
// Actor+0x36. Discovered by memory scan (scratch/az_playeryaw_scan.py,
// 2026-07-03): during walk_forward from Link's House the s16 at 0x36
// transitions -32767 → +110 on frame [01] (~180° flip to face motion),
// matching SoH's Actor.world.rot.y transition from -32768 → +108 to
// within 2 binary-angle units (float rounding).
//
// Two other candidates flipped in the same scan (0xBE, 0x3CA) — 0xBE
// pre-flipped by the first snapshot (probably targetYaw / actionYaw),
// 0x3CA snapped to exactly 0 (likely a related but not-yet-named field
// like the Player-struct's own yaw goal). Only 0x36 is committed as
// the live-facing yaw; d4 in firstdiv should switch to reading this
// offset on the Az side so the compare doesn't fabricate a rotation
// divergence from the static spawn-rot field.
constexpr uint32_t PLAYER_YAW_OFF           = 0x0036;

// Actor.bgCheckFlags — u16 bitfield of "what am I touching" queried by
// Player_Update to gate wall-slide, wall-climb, jump-off-ledge, etc.
// SoH-N64's docblock (z64actor.h:277-288):
//   bit 0x001  standing on the ground
//   bit 0x008  touching a wall
//   bit 0x010  touching a ceiling
//   bit 0x080  similar to 0x001 but with no velocity check
//   bit 0x200  set/used only by Player
// Discovered by 3-state wall-walk scan (scratch/az_bgflags_scan.py +
// az_bgflags_confirm.py, 2026-07-03): at Actor+0x0090 the u16 transitions
//   rest (Link at spawn, standing):  0x0081  (0x01 + 0x80)
//   wall (walked into wall, held):   0x0289  (0x01 + 0x08 + 0x80 + 0x200)
//   released (stick zeroed, on gnd): 0x0081  (wall bit cleared)
// Byte-for-byte matches SoH-N64's bgCheckFlags bit pattern. GREZZO shifted
// the field from N64's 0x088 by +8 bytes; probably one extra pointer/f32
// slot inserted earlier in Actor.
constexpr uint32_t ACTOR_BGCHECKFLAGS_OFF   = 0x0090;
constexpr uint16_t BGCF_WALL                = 0x0008;
constexpr uint16_t BGCF_GROUND              = 0x0001;
constexpr uint32_t TRANSITION_TRIGGER_OFF  = 0x5C2D;
constexpr uint32_t NEXT_ENTRANCE_OFF       = 0x5C32;
constexpr uint8_t  TRANS_TRIGGER_START     = 20;

// OoT3D play-mode active camera basis inside PlayState. Located by a live
// memory scan (scratch/scan_azcam.py-style probe) for eye=(0,34,0) at
// Link's House frame 60 post-loadstate, cross-checked against SoH's
// mainCamera state. The scan surfaced three matching Vec3f-triple blocks
// inside PlayState at +0x1B8, +0x3E4, +0x408; the first is mainCamera,
// the other two look like subCameras / eyeNext copies. GREZZO reordered
// the fields vs the N64 Camera struct (which packs at at +0x50, eye at
// +0x5C, up at +0x68); on 3DS the anchor block is packed EYE → AT → UP
// starting at PlayState+0x1B8.
//
// Verified at Link's House frame 60:
//   ps+0x1B8: eye = (0.000, 34.000, 0.000)
//   ps+0x1C4: at  = (1.064, 34.010, 100.885)
//   ps+0x1D0: up  = (0.000, 1.000, 0.000)
// which matches SoH's SohState_Camera output byte-for-byte modulo FP
// rounding.
constexpr uint32_t PLAY_CAM_EYE_OFF        = 0x01B8;
constexpr uint32_t PLAY_CAM_AT_OFF         = 0x01C4;
constexpr uint32_t PLAY_CAM_UP_OFF         = 0x01D0;

// ── OoT3D mainCamera pointer + setting/mode ─────────────────────────────
// mainCamera is HEAP-ALLOCATED, not embedded. The array of Camera pointers
// (cameraPtrs[MAIN_CAM..]) lives at PlayState+0xA54 — RE'd from
// FUN_002d84c4 line 88 (`*(int*)(*(int*)(param_2 + 0xd4) + 0xa54) + 0xd8`
// chains play→cameraPtrs→mainCamera→sOOBTimer holder) and from
// FUN_002e2e60 line 793 (Play_UpdateMainCamera caller dispatches
// cameraPtrs[i] into FUN_002d84c4).
//
// Inside a Camera struct:
//   Camera+0x188 = status (s16)
//   Camera+0x18A = setting (low byte of u16)
//   Camera+0x18C = mode    (low byte of u16)
// (Also RE'd from FUN_002d84c4's indirect dispatch at line 217-219.)
//
// So the read chain is:
//   cam_ptr = *(u32*)(PlayState + 0xA54)
//   setting = *(u16*)(cam_ptr + 0x18A) & 0xFF
//
// The eye Vec3f at PLAY_CAM_EYE_OFF = 0x1B8 is play->view.eye (the view
// basis written back by Camera_Update's tail via func_800AA358) — NOT
// Camera->eye. The initial harness scan matched view.eye against
// SohState_Camera, which returns view.eye too — that's why both sides
// agreed byte-for-byte at Link's House.
constexpr uint32_t PLAY_CAMERAPTRS_OFF     = 0x0A54;
constexpr uint32_t CAMERA_STATUS_OFF       = 0x0188;
constexpr uint32_t CAMERA_MODE_OFF         = 0x018C;

// ── Per-dimension sign-convention invariant ─────────────────────────────
// Every position/basis compare between Az and SoH implicitly assumes the two
// engines share coordinate conventions. Empirically verified true at Link's
// House:
//   - Player spawn matches at (+1, 0, +95) on both engines (d3 baseline).
//   - Camera basis matches: eye=(0,34,0) at=(1.06,34.01,100.89) up=(0,1,0)
//     on both engines (d5 baseline).
//   - Under matched-convention "forward" input (soh_input 0 0 +127 +
//     analog 0 -32000), both engines walk +Z ~36-40 units in one direction
//     (scratch/direction_matched.py, 2026-07-03).
//
// So all AZ_*_SIGN_FLIP flags below are `false` today. This is deliberately
// a per-axis descriptor so if OoT3D later turns out to flip a specific
// world axis (e.g. camera Up on a scene that inverts) or if we discover a
// per-actor sign-convention difference, flipping ONE flag corrects every
// downstream compare in one seam instead of ad-hoc per test. `false` = no
// runtime effect; the multiply-by-+1 constant-folds.
// ── Firstdiv sign-blind policy ──────────────────────────────────────────
// Under matched drive + game-frame alignment + d4-yaw-fix, the surviving
// firstdiv divergences at Link's House are all project-vision decisions,
// not port bugs. This policy classifies each would-be divergence so the
// tool stops re-reporting known-classified items and the next real port
// gap stands out.
//
// Two distinct classes:
//
//   PermanentNoise — a divergence class that will NEVER be fixed and
//   should be silenced in every scene forever:
//     · Rate-compensation: SoH's Player_Update at 20fps + documented
//       1.5× pos-per-frame multiplier vs Az at 30fps. When both engines'
//       speedXZ match, any Δpos is accumulated tick-rate difference.
//     · Wonder_Talk2 3DS content addition (cat=1 id=0x0185) — d6 count
//       delta of exactly one extra Az cat=1 actor of that id.
//     · Navi RNG (cat=7 id=0x0018) — d7 worst_pos_drift on that pair.
//
//   DeferredPortTarget — a divergence class that IS a real port bug and
//   WILL be worked on later (specifically the scene-collision porting
//   arc). Silenced NOW to advance the compare sweep past the wall-stop
//   plateau, but tracked as real work — do NOT confuse with permanent
//   noise:
//     · Scene-collision wall stop: SoH keeps N64 room binaries and stops
//       Link at Z=131 in Link's House; OoT3D's collision has the wall
//       at Z=135.5. Real port target; deferred to the scene-collision
//       arc. Detected when SoH's Player bgCheckFlags & 0x008 is set.
//     · Wall-slide yaw drift downstream of the above.
//
// Reporting: when a divergence is classified, print the class + reason
// inline on the dimension's own line, but DO NOT fire fd.report. The
// firstdiv summary still shows "none" when only classified items were
// found, so the tool signals "advance to next scene / next drive" clearly.
enum class DivClass : int {
    Unclassified       = 0,
    PermanentNoise     = 1,
    DeferredPortTarget = 2,
};
struct DivDecision {
    DivClass cls = DivClass::Unclassified;
    const char* tag = "";       // short label, e.g. "rate-comp", "collision-wall"
    // Origin annotation — this is the manager-directed extension so the
    // tool tells the human WHERE the classification came from without a
    // manual chase. Each field points at either a RE'd memory offset or
    // an in-doc RE journal entry.
    const char* origin_az   = ""; // e.g. "Actor+0x068 (speedXZ)"
    const char* origin_soh  = ""; // e.g. "Actor.speedXZ (z64actor.h:227)"
    const char* origin_doc  = ""; // e.g. "oot3d-decomp/docs/gameplay_firstdiv.md#speedXZ"
};
constexpr DivDecision kUnclassified{DivClass::Unclassified, "", "", "", ""};
inline const char* DivClassStr(DivClass c) {
    switch (c) {
        case DivClass::PermanentNoise:     return "NOISE";
        case DivClass::DeferredPortTarget: return "DEFERRED";
        default:                            return "";
    }
}
// Wonder_Talk2 3DS content addition — filters cat=1 id=0x0185.
inline DivDecision ClassifyD6Content(int cat_delta_idx, int az_cnt, int soh_cnt,
                                     int wonder_talk2_extra_az) {
    // Called with the summarized: was the only mismatch exactly one extra
    // Az cat=1 actor of id 0x0185? If so, PermanentNoise.
    if (wonder_talk2_extra_az == 1 &&
        (az_cnt - soh_cnt) == 1 && cat_delta_idx == 1)
        return {DivClass::PermanentNoise, "wonder_talk2",
                "cat=1 id=0x0185 (En_Wonder_Talk2)",
                "no SoH equivalent — 3DS-only",
                "oot3d-decomp/docs/gameplay_firstdiv.md#sign-blind-policy"};
    return kUnclassified;
}
// Navi RNG drift — filter cat=7 id=0x0018.
inline DivDecision ClassifyD7Worst(int worstCat, int worstId) {
    if (worstCat == 7 && worstId == 0x0018)
        return {DivClass::PermanentNoise, "navi-rng",
                "cat=7 id=0x0018 (En_Elf/Navi)",
                "same (En_Elf) — RNG seed divergence",
                "oot3d-decomp/docs/gameplay_firstdiv.md#rng-determinism"};
    return kUnclassified;
}
// d3 Player-pos classifier. Uses live speedXZ readback on both engines
// + bgCheckFlags on BOTH sides (Az RE'd at Actor+0x0090, soh3d b50560b+)
// to decide: wall-stop deferred vs matched-rate noise vs unclassified.
inline DivDecision ClassifyD3PlayerPos(float soh_speedXZ, float az_speedXZ,
                                        unsigned int soh_bgFlags,
                                        unsigned int az_bgFlags) {
    const bool soh_walled = (soh_bgFlags & 0x0008u) != 0;
    const bool az_walled  = (az_bgFlags  & 0x0008u) != 0;
    if (soh_walled || az_walled) {
        return {DivClass::DeferredPortTarget, "collision-wall",
                "Player Actor+0x0090 bgCheckFlags & 0x008 (soh3d TBD)",
                "Actor.bgCheckFlags & 0x008 (z64actor.h:237, :281)",
                "oot3d-decomp/docs/gameplay_firstdiv.md#scene-collision"};
    }
    // Both engines' speedXZ matched → Δpos is tick-rate accumulation
    // (SoH runs Player_Update at 20fps × 1.5 pos multiplier vs Az at
    // 30fps). Tolerance 2.0 units per game frame accounts for
    // acceleration-curve alignment lag.
    if (std::fabs(soh_speedXZ - az_speedXZ) < 2.0f &&
        soh_speedXZ > 0.1f && az_speedXZ > 0.1f) {
        return {DivClass::PermanentNoise, "rate-comp",
                "Player Actor+0x068 speedXZ (soh3d f70e927)",
                "Actor.speedXZ (z64actor.h:227)",
                "oot3d-decomp/docs/gameplay_firstdiv.md#per-frame-firstdiv"};
    }
    return kUnclassified;
}
constexpr bool AZ_POS_X_SIGN_FLIP    = false;
constexpr bool AZ_POS_Y_SIGN_FLIP    = false;
constexpr bool AZ_POS_Z_SIGN_FLIP    = false;
constexpr bool AZ_CAM_X_SIGN_FLIP    = false;
constexpr bool AZ_CAM_Y_SIGN_FLIP    = false;
constexpr bool AZ_CAM_Z_SIGN_FLIP    = false;

// Convenience: signed multiplier for a bool flag (constant-folds to +1/-1).
constexpr float FlipMul(bool flip) { return flip ? -1.0f : +1.0f; }

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

// Dump title-demo pose entries. Reads the SkelAnime pose stream for one of
// the two statically-pre-allocated demo actors: `titleactors [a|b]` (default
// a = Epona, b = the sibling demo actor at 0x005A54D8). Multiline reply,
// terminated by `ok end`. Only meaningful at title (TitleActive()).
void HandleTitleActors(std::istringstream& toks) {
    std::string which; toks >> which;
    if (which.empty()) which = "a";
    uint32_t base, count;
    const char* tag;
    if (which == "a") {
        base = TITLE_POSE_TABLE_VA; count = TITLE_POSE_COUNT; tag = "epona";
    } else if (which == "b") {
        base = TITLE_POSE_TABLE_B_VA; count = TITLE_POSE_B_COUNT; tag = "sibling";
    } else {
        PrintErr("titleactors: usage: titleactors [a|b]"); return;
    }
    if (!TitleActive()) {
        PrintErr("titleactors: not at title (scene!=0x51 or active flag clear)");
        return;
    }
    auto& mem = Core::System::GetInstance().Memory();
    std::printf("ok titleactors %s %u\n", tag, count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t va = base + i * TITLE_POSE_STRIDE;
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

// SoH-only warp: schedule a scene transition on the SoH side without
// touching Azahar. Needed when we've put Azahar into the target scene
// via a savestate and now want SoH to warp there independently — the
// combined `force warp` writes both, which re-triggers Azahar's
// already-loaded Play and (empirically) sends it back to title.
void HandleSohWarp(std::istringstream& toks) {
    std::string ent_s;
    if (!(toks >> ent_s)) { PrintErr("soh_warp: usage: soh_warp <entrance>"); return; }
    auto ent = ParseNum(ent_s);
    if (!ent) { PrintErr("soh_warp: bad entrance"); return; }
    if (!g_soh_booted) { PrintErr("soh_warp: run soh_boot first"); return; }
    if (!SohState_Warp(static_cast<unsigned short>(*ent & 0xFFFF))) {
        PrintErr("soh_warp: no playstate — soh_step until Play is up first");
        return;
    }
    std::printf("ok soh_warp 0x%04x\n", static_cast<unsigned>(*ent & 0xFFFF));
}

// soh_setage <0|1>  — write gSaveContext.linkAge (0=adult, 1=child).
// Camera_CalcAtDefault / Player_GetHeight consume this synchronously
// via LINK_IS_ADULT. Use BEFORE soh_warp so the newly-spawned Player
// picks up the age. Kakariko sweep uses this to match Azahar's
// linkshouse savestate (child) — see gameplay_firstdiv.md ROOT CAUSE.
void HandleSohSetAge(std::istringstream& toks) {
    std::string age_s;
    if (!(toks >> age_s)) {
        PrintErr("soh_setage: usage: soh_setage <0|1>");
        return;
    }
    auto age = ParseNum(age_s);
    if (!age || (*age != 0 && *age != 1)) {
        PrintErr("soh_setage: bad age (must be 0 or 1)");
        return;
    }
    if (!g_soh_booted) {
        PrintErr("soh_setage: run soh_boot first");
        return;
    }
    SohState_SetLinkAge(static_cast<int>(*age));
    int now = SohState_GetLinkAge();
    std::printf("ok soh_setage %ld (%s)  now=%d\n",
                static_cast<long>(*age),
                *age == 0 ? "adult" : "child", now);
}

void HandleSohGetAge(std::istringstream&) {
    if (!g_soh_booted) { PrintErr("soh_getage: run soh_boot first"); return; }
    int age = SohState_GetLinkAge();
    std::printf("ok soh_getage %d (%s)\n",
                age, age == 0 ? "adult" : "child");
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
    // 3ds side: at title, the camera basis lives at 0x005BE6D4 as
    // eye + dir(unit) + up(unit) — 3 consecutive Vec3f. Located by
    // find_cam_eye.py: scan for isolated Vec3f with X in [-6000,-3000]
    // Y small, Z in [3000,7000], NOT part of a repeating stride table,
    // and where +12 is a unit vector; that landed 0x005BE6D4 with
    // eye ≈ (-4070, 58, 5219), dir ≈ (-0.45, 0.09, -0.89), up ≈ (0.21, 0.98, -0.01).
    // Matches SoH's title camera magnitudes closely. Non-title state
    // may use a different addr; for now this covers d5 at title only.
    {
        auto& mem = Core::System::GetInstance().Memory();
        constexpr uint32_t AZ_CAM_VA = 0x005BE6D4;
        float e[3], d[3], u[3];
        bool ok = true;
        for (int j = 0; j < 3; ++j) {
            auto ev = mem.Read32OrNullopt(AZ_CAM_VA + 0  + j*4);
            auto dv = mem.Read32OrNullopt(AZ_CAM_VA + 12 + j*4);
            auto uv = mem.Read32OrNullopt(AZ_CAM_VA + 24 + j*4);
            if (!ev || !dv || !uv) { ok = false; break; }
            std::memcpy(&e[j], &*ev, 4);
            std::memcpy(&d[j], &*dv, 4);
            std::memcpy(&u[j], &*uv, 4);
        }
        if (ok) {
            std::printf("  3ds (title, 0x%08x): eye=(%.2f,%.2f,%.2f) dir=(%.3f,%.3f,%.3f) up=(%.3f,%.3f,%.3f)\n",
                        AZ_CAM_VA, e[0],e[1],e[2], d[0],d[1],d[2], u[0],u[1],u[2]);
        } else {
            std::printf("  3ds: n/a (0x%08x unmapped)\n", AZ_CAM_VA);
        }
    }
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
    // 3ds side — envCtx offset RE'd 2026-07-04 via harness cursor-diff +
    // JIT watchpoint + Ghidra decomp of FUN_0045dd30 (Environment_Update).
    // envCtx base = play + 0x3135; unk_BF (current slot) at +0xA5;
    // shadow at +0xA6; lerp weight (float) at +0xC8. Palette lookup uses
    // 0x1C stride. See oot3d-decomp/docs/env_context_layout.md.
    auto& mem = Core::System::GetInstance().Memory();
    // gPlayState pointer at GPLAYSTATE_VA can lag until boot completes,
    // but the play struct itself is heap-alloc'd at a stable VA the RE
    // scripts rely on. Try the indirection first; fall back to the fixed
    // VA so `compare lighting` works during the same early window that
    // `mem 0x0871E840 …` already reads from in the sweep scripts.
    constexpr uint32_t AZ_PLAY_STRUCT_VA = 0x0871E840;
    auto p = mem.Read32OrNullopt(GPLAYSTATE_VA);
    uint32_t play_va = (p && *p != 0) ? *p : AZ_PLAY_STRUCT_VA;
    {
        const uint32_t env_base = play_va + 0x3135;
        // Read the u8 fields via Read32OrNullopt on the containing word,
        // then extract the byte at the right sub-offset. Read8OrNullopt
        // doesn't exist in this Azahar branch.
        auto w_a4 = mem.Read32OrNullopt(env_base + 0xA4);         // covers +A4..+A7
        auto w_cc = mem.Read32OrNullopt(env_base + 0xCC);         // covers +CC..+CF
        auto weight_i = mem.Read32OrNullopt(env_base + 0xC8);
        auto byte_from = [](std::optional<uint32_t> w, uint32_t sub) -> unsigned {
            if (!w) return 0xFFu;
            return (unsigned)((*w >> (sub * 8)) & 0xFFu);
        };
        float weight = 0.0f;
        if (weight_i) std::memcpy(&weight, &*weight_i, sizeof(float));
        std::printf("  3ds: envCtx@0x%08x  slot=%u  prevSlot=%u  lerpWeight=%.3f  mode=0x%02x\n",
                    (unsigned)env_base,
                    byte_from(w_a4, 1),   // +0xA5 = current slot
                    byte_from(w_a4, 2),   // +0xA6 = shadow slot
                    weight_i ? weight : 0.0f,
                    byte_from(w_cc, 0));  // +0xCC = mode/flag
    }
    // soh side
    if (!SohState_HasPlayState()) {
        std::printf("  soh: n/a (no playstate)\n");
        return;
    }
    unsigned char ambient[3], l1c[3], l2c[3], fog[3], lcAmb[3], lcFog[3];
    signed char l1d[3], l2d[3];
    short fogNear, fogFar, lcFogNear, lcFogFar;
    unsigned char sohBF = 0xFF, sohBD = 0xFF;
    float sohD8 = 0.0f;
    if (!SohState_Lighting(ambient, l1d, l1c, l2d, l2c, fog, &fogNear, &fogFar,
                          lcAmb, lcFog, &lcFogNear, &lcFogFar,
                          &sohBF, &sohBD, &sohD8)) {
        std::printf("  soh: n/a (SohState_Lighting failed)\n");
        return;
    }
    std::printf("  soh: slot=%u  prevSlot=%u  lerpWeight=%.3f\n"
                "       envLightSettings ambient=(%u,%u,%u) fog=(%u,%u,%u) fogNear=%d fogFar=%d\n"
                "       light1 dir=(%d,%d,%d) color=(%u,%u,%u)\n"
                "       light2 dir=(%d,%d,%d) color=(%u,%u,%u)\n",
                (unsigned)sohBF, (unsigned)sohBD, sohD8,
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
// ─── compare firstdiv ────────────────────────────────────────────────────────
// Structured dimension sweep. Walks a fixed list of comparable fields at
// this frame and reports THE FIRST field where the two engines diverge.
// Naming the divergence is the deliverable; visual PPM eyeballing is not
// how parity closes here (manager doctrine 2026-07-03).
//
// Dimensions ordered outer-to-inner by expected divergence blast radius:
//   1. gamestate mode      (title vs Play — must both be at title)
//   2. sceneNum
//   3. Link ↔ Link limb count (25 3ds table B vs 22 soh Player)
//   4. per-limb rotation delta after 3DS-rad → N64-binary-angle conversion
//   5. camera basis (RE'd on both sides — currently 3ds unavailable; skip)
//
// Emits `firstdiv: <field> <details>` on the first mismatch and stops,
// or `firstdiv: none` if everything checked matched within tolerance.

// 3DS radians → N64 binary angle (0..0x10000 covers 0..2π). Wraps into
// s16 by taking the low 16 bits — the same convention SoH uses for
// Vec3s rot fields.
static short RadToBinaryAngle(float rad) {
    // 0x10000 / (2π) ≈ 10430.378
    const float scale = 10430.378350470453f;
    float x = rad * scale;
    // Wrap to [-32768, 32767]
    long long q = static_cast<long long>(x);
    return static_cast<short>(q & 0xFFFF);
}

struct FirstDivReporter {
    bool reported = false;
    void report(const char* field, const std::string& details) {
        if (reported) return;
        std::printf("  firstdiv: %s %s\n", field, details.c_str());
        reported = true;
    }
};

void CompareFirstDivImpl() {
    FirstDivReporter fd;
    // Dim 1: gamestate mode
    const bool az_at_title  = TitleActive();
    const bool soh_at_title = SohState_HasPlayState() &&
                              (SohState_SceneNum() & 0xFFFF) == 0x51;
    const bool soh_in_play  = SohState_HasPlayState();
    auto ps_az              = CurrentPlayState();
    const bool az_in_play   = !az_at_title && ps_az.has_value();
    std::printf("  d1 gamestate:    az=%s soh=%s\n",
                az_at_title ? "title(inline)" :
                az_in_play  ? "play"          : "n/a",
                soh_at_title ? "play(scene=0x51)" :
                soh_in_play  ? "play"             : "not-title");
    if (az_at_title != soh_at_title && !(az_in_play && soh_in_play)) {
        char buf[128]; std::snprintf(buf, sizeof buf,
            "az_title=%d soh_title=%d az_play=%d soh_play=%d "
            "— engines in different gamestate machinery",
            (int)az_at_title, (int)soh_at_title,
            (int)az_in_play, (int)soh_in_play);
        fd.report("gamestate-mode", buf);
    }

    // Dim 2: sceneNum
    auto& mem = Core::System::GetInstance().Memory();
    unsigned az_scene = 0xFFFF, soh_scene = 0xFFFF;
    if (az_at_title) az_scene = 0x51;  // inline title always 0x51 at title
    else if (ps_az) {
        auto sn = mem.Read32OrNullopt(*ps_az + SCENENUM_OFF);
        if (sn) az_scene = *sn & 0xFFFF;
    }
    if (SohState_HasPlayState()) soh_scene = SohState_SceneNum() & 0xFFFF;
    std::printf("  d2 sceneNum:     az=0x%04x soh=0x%04x\n", az_scene, soh_scene);
    if (!fd.reported && az_scene != soh_scene) {
        char buf[128]; std::snprintf(buf, sizeof buf,
            "az=0x%04x soh=0x%04x", az_scene, soh_scene);
        fd.report("sceneNum", buf);
    }

    // Are both engines in the SAME gameplay scene (Play mode, not title-demo)?
    // If so, route d3..d5 through the play-mode checks below and skip the
    // title-only pose/camera work at the bottom.
    const bool both_in_play_gameplay = az_in_play && soh_in_play &&
                                       az_scene != 0xFFFF && az_scene != 0x51 &&
                                       az_scene == soh_scene;
    if (both_in_play_gameplay) {
        // Hoisted classification decision — d3 populates it, d4/d5 consult
        // it (their divergences are typically downstream of d3).
        DivDecision d3_decision = kUnclassified;
        // Track Az Player addr across firstdiv calls so bgCheckFlags watch
        // auto-registers once per scene load. Kept static to survive across
        // calls; if the Player addr changes (scene reload) the new addr
        // triggers a fresh RegisterWatchpoint.
        static uint32_t s_watched_player_addr = 0;
        static uint32_t s_watched_bgflag_addr = 0;
        static uint32_t s_watched_speed_addr  = 0;
        static uint32_t s_watched_yaw_addr    = 0;
        static uint32_t s_watched_cam_eye_addr = 0;
        static uint32_t s_watched_deltaA_addr = 0;
        // Look up Az Player addr this call.
        {
            uint32_t az_player_addr = 0;
            const uint32_t ctx = *ps_az + ACTORCTX_OFF;
            auto head = mem.Read32OrNullopt(ctx + ACTOR_LISTS_OFF + 2 * 8 + 4);
            if (head && *head != 0) az_player_addr = *head;
            if (az_player_addr != 0 && az_player_addr != s_watched_player_addr) {
                // Scene load changed the Player Actor addr — re-register
                // watches on the three RE'd Player fields we consult in
                // classifier auto-attach: bgCheckFlags, speedXZ, yaw.
                // Same page-granular watchpoint mechanism; each RE'd
                // offset gets its own range slot for RangeKey lookup.
                auto reregister = [&](uint32_t& slot, uint32_t new_addr,
                                       uint32_t size) {
                    if (slot != 0 && Soh3d_WatchIsRegistered(slot)) {
                        Soh3d_WatchRemoveRange(slot, size);
                    }
                    slot = new_addr;
                    Soh3d_WatchAddRange(slot, size);
                };
                s_watched_player_addr = az_player_addr;
                reregister(s_watched_bgflag_addr,
                            az_player_addr + ACTOR_BGCHECKFLAGS_OFF, 2);
                reregister(s_watched_speed_addr,
                            az_player_addr + ACTOR_SPEEDXZ_OFF, 4);
                reregister(s_watched_yaw_addr,
                            az_player_addr + PLAYER_YAW_OFF, 2);
                // Camera eye lives on PlayState, not Player. Piggyback on
                // scene-load (Player-addr change) to also refresh a 12-byte
                // watchpoint covering the full eye Vec3f — Kakariko sweep
                // (2026-07-03) surfaced d5 |Δeye|=28 with matching Link
                // pos+yaw, so the OoT3D camera update site is the new port
                // frontier for d5.
                //
                // Prefer cam_ptr+0x8C (the actual heap Camera's eye field) so
                // captured writer PCs land INSIDE the mode function (e.g.
                // Camera_Normal1 @ 0x00239fd8) rather than in Camera_Update's
                // mode-agnostic tail (0x002d92a4). Fall back to PS+0x1B8 iff
                // cam_ptr isn't set up yet.
                {
                    auto cp = mem.Read32OrNullopt(
                        *ps_az + PLAY_CAMERAPTRS_OFF);
                    uint32_t eye_watch_addr = (cp && *cp != 0)
                        ? (*cp + 0x8C)
                        : (*ps_az + PLAY_CAM_EYE_OFF);
                    reregister(s_watched_cam_eye_addr, eye_watch_addr, 12);
                }
                // Δ-A activation watch: catch any write to the state word
                // at Player+0x29B8. When bit 0x100 gets set, OoT3D's
                // Camera_CalcAtDefault adds an extra Y bias to at.y —
                // a real code divergence vs SoH (see docs Δ-A). We know
                // the block is INERT at Kakariko-idle; this watch lets a
                // future scene sweep catch the guest PC that SETS the bit
                // (climbing / pull / grab candidates), identifying the
                // upstream Player state machine that owns the flag.
                reregister(s_watched_deltaA_addr,
                            az_player_addr + 0x29B8, 4);
            }
        }
        // ── Play-mode d3: Link position match ────────────────────────────
        // Walk Azahar's actor table for cat=2 (Player) id=0.
        const uint32_t ctx = *ps_az + ACTORCTX_OFF;
        bool az_player_found = false;
        float az_px=0, az_py=0, az_pz=0;
        short az_rx=0, az_ry=0, az_rz=0;
        for (uint32_t cat = 0; cat < 12 && !az_player_found; ++cat) {
            auto head = mem.Read32OrNullopt(ctx + ACTOR_LISTS_OFF + cat * 8 + 4);
            if (!head || *head == 0) continue;
            auto id = mem.Read32OrNullopt(*head + ACTOR_ID_OFF);
            if (!id || (*id & 0xFFFF) != 0) continue;
            auto rx = mem.Read32OrNullopt(*head + ACTOR_POS_OFF + 0);
            auto ry = mem.Read32OrNullopt(*head + ACTOR_POS_OFF + 4);
            auto rz = mem.Read32OrNullopt(*head + ACTOR_POS_OFF + 8);
            auto rr = mem.Read32OrNullopt(*head + ACTOR_ROT_OFF + 0);
            auto rr2= mem.Read32OrNullopt(*head + ACTOR_ROT_OFF + 4);
            if (!rx || !ry || !rz || !rr || !rr2) break;
            std::memcpy(&az_px, &*rx, 4);
            std::memcpy(&az_py, &*ry, 4);
            std::memcpy(&az_pz, &*rz, 4);
            az_rx = static_cast<short>(*rr & 0xFFFF);
            az_ry = static_cast<short>((*rr >> 16) & 0xFFFF);
            az_rz = static_cast<short>(*rr2 & 0xFFFF);
            // OoT3D decoupled Actor.world.rot.y (static spawn value) from
            // the live-facing yaw the Player_Update writes each frame.
            // Live yaw lives at Player+0x36 (see PLAYER_YAW_OFF block).
            // Without this override d4 fabricates a ~32700 rotation
            // divergence on every startup because it reads the spawn slot.
            auto yaw_u32 = mem.Read32OrNullopt(*head +
                (PLAYER_YAW_OFF & ~3u));
            if (yaw_u32) {
                az_ry = static_cast<short>(
                    (*yaw_u32 >> ((PLAYER_YAW_OFF & 2) * 8)) & 0xFFFF);
            }
            az_player_found = true;
        }
        float soh_px=0, soh_py=0, soh_pz=0;
        short soh_rx=0, soh_ry=0, soh_rz=0;
        bool soh_player_found = SohState_PlayerPos(&soh_px, &soh_py, &soh_pz,
                                                    &soh_rx, &soh_ry, &soh_rz) != 0;
        if (!az_player_found || !soh_player_found) {
            std::printf("  d3 player pos:   az=%s soh=%s\n",
                        az_player_found ? "found" : "no Player actor",
                        soh_player_found ? "found" : "no Player actor");
            if (!fd.reported) fd.report("player-actor",
                az_player_found ? "SoH has no live Player actor" :
                                  "Azahar has no cat=2 id=0 actor");
        } else {
            // Apply the per-axis position sign-flip invariant (all false
            // today; see AZ_POS_*_SIGN_FLIP block near the RE offsets).
            const float az_px_n = az_px * FlipMul(AZ_POS_X_SIGN_FLIP);
            const float az_py_n = az_py * FlipMul(AZ_POS_Y_SIGN_FLIP);
            const float az_pz_n = az_pz * FlipMul(AZ_POS_Z_SIGN_FLIP);
            float dp = std::sqrt((az_px_n-soh_px)*(az_px_n-soh_px)
                               + (az_py_n-soh_py)*(az_py_n-soh_py)
                               + (az_pz_n-soh_pz)*(az_pz_n-soh_pz));
            std::printf("  d3 player pos:   az=(%.1f,%.1f,%.1f) soh=(%.1f,%.1f,%.1f) "
                        "|Δ|=%.2f\n",
                        az_px, az_py, az_pz, soh_px, soh_py, soh_pz, dp);
            if (dp > 1.0f) {
                // Classify before firing. Reads live speedXZ on both sides
                // + SoH bgCheckFlags to decide rate-comp NOISE vs
                // collision-wall DEFERRED vs unclassified.
                float soh_spdXZ = 0.0f, soh_velY = 0.0f;
                unsigned int soh_bg = 0;
                int wY = 0, wBg = 0;
                unsigned long wP = 0;
                SohState_PlayerWallInfo(&soh_bg, &wY, &wBg, &wP,
                                         &soh_spdXZ, &soh_velY);
                float az_spdXZ = 0.0f;
                unsigned int az_bg = 0;
                if (az_player_found) {
                    // Read Az Player speedXZ + bgCheckFlags. Walk player_actor
                    // addr by repeating the same cat=2 lookup used above.
                    auto head = mem.Read32OrNullopt(ctx + ACTOR_LISTS_OFF +
                                                    2 * 8 + 4);
                    if (head && *head != 0) {
                        auto sv = mem.Read32OrNullopt(*head +
                                                      ACTOR_SPEEDXZ_OFF);
                        if (sv) std::memcpy(&az_spdXZ, &*sv, 4);
                        auto bv = mem.Read32OrNullopt(*head +
                                                      (ACTOR_BGCHECKFLAGS_OFF & ~3u));
                        if (bv) {
                            unsigned int raw = *bv;
                            // bgCheckFlags is u16 at 0x0090; that's aligned
                            // to the u32 low half.
                            az_bg = raw & 0xFFFFu;
                        }
                    }
                }
                d3_decision = ClassifyD3PlayerPos(soh_spdXZ, az_spdXZ,
                                                    soh_bg, az_bg);
                if (d3_decision.cls != DivClass::Unclassified) {
                    std::printf("    d3 classified: %s (%s) — "
                                "soh_v=%.2f az_v=%.2f "
                                "soh_bgW=%d az_bgW=%d\n"
                                "      origin: az=%s  soh=%s  doc=%s\n",
                                DivClassStr(d3_decision.cls), d3_decision.tag,
                                soh_spdXZ, az_spdXZ,
                                (int)((soh_bg & 0x0008u) != 0),
                                (int)((az_bg & 0x0008u) != 0),
                                d3_decision.origin_az,
                                d3_decision.origin_soh,
                                d3_decision.origin_doc);
                    // Manager-directed auto-attach: on collision-wall
                    // classify, query the most recent write to Az's
                    // bgCheckFlags that has bit 0x08 set — that's the
                    // guest instruction that flagged wall-touching.
                    // Ghidra-jumping to writer_pc lands on OoT3D's
                    // wall-touch handler; cross-check vs SoH's equivalent
                    // closes the RE-first loop.
                    // Route each classifier tag to the relevant field's
                    // ring buffer.
                    uint32_t query_addr = 0;
                    uint64_t match_mask = 0, match_expected = 0;
                    const char* hint = "";
                    if (d3_decision.cls == DivClass::DeferredPortTarget) {
                        // collision-wall: look for the write that set
                        // bit 0x08 on bgCheckFlags.
                        query_addr    = s_watched_bgflag_addr;
                        match_mask    = 0x0008u;
                        match_expected = 0x0008u;
                        hint          = "Ghidra-jump this PC for OoT3D's "
                                        "wall-touch handler";
                    } else if (d3_decision.tag[0] != '\0' &&
                                d3_decision.tag[0] == 'r' /* rate-comp */) {
                        // rate-comp: latest speedXZ write. No bit-mask
                        // predicate — just the most recent write to
                        // Actor+0x0068 (mask=0, expected=0).
                        query_addr    = s_watched_speed_addr;
                        hint          = "Ghidra-jump this PC for OoT3D's "
                                        "Player_Update speedXZ integrate";
                    }
                    if (query_addr != 0) {
                        WatchRecord rec;
                        if (Soh3d_WatchGetLatestMatching(
                                query_addr, match_mask, match_expected, &rec)) {
                            std::printf("      writer:  az_pc=0x%08x lr=0x%08x "
                                        "data=0x%016lx ticks=%lu — %s\n",
                                        rec.arm_pc, rec.arm_lr,
                                        (unsigned long)rec.data,
                                        (unsigned long)rec.cycles, hint);
                        } else {
                            // Fall back to most-recent-any-write on the
                            // same range for triage.
                            WatchRecord any;
                            if (Soh3d_WatchGetLatestMatching(
                                    query_addr, 0u, 0u, &any)) {
                                std::printf("      writer:  (predicate not "
                                            "yet matched; last write) "
                                            "az_pc=0x%08x lr=0x%08x data=0x%016lx\n",
                                            any.arm_pc, any.arm_lr,
                                            (unsigned long)any.data);
                            } else {
                                std::printf("      writer:  no hits recorded "
                                            "on 0x%08x — watch may not be "
                                            "firing (check AZAHAR_PATCH.md)\n",
                                            query_addr);
                            }
                        }
                    }
                } else if (!fd.reported) {
                    char buf[192]; std::snprintf(buf, sizeof buf,
                        "|Δpos|=%.2f az=(%.1f,%.1f,%.1f) soh=(%.1f,%.1f,%.1f)",
                        dp, az_px_n, az_py_n, az_pz_n, soh_px, soh_py, soh_pz);
                    fd.report("player-pos", buf);
                }
            }

            // ── Play-mode d4: Link rotation match ────────────────────────
            // s16 Vec3s rot: X,Y,Z on both sides (OoT3D packs rx/ry into 4B,
            // rz into next 4B; reads above already unpacked to shorts).
            int drx = (int)az_rx - (int)soh_rx;
            int dry = (int)az_ry - (int)soh_ry;
            int drz = (int)az_rz - (int)soh_rz;
            auto wrap = [](int d){
                if (d >  32768) d -= 65536;
                if (d < -32768) d += 65536;
                return d < 0 ? -d : d;
            };
            int adrx = wrap(drx), adry = wrap(dry), adrz = wrap(drz);
            int worstAxis = 0; int worstD = adrx;
            if (adry > worstD) { worstAxis = 1; worstD = adry; }
            if (adrz > worstD) { worstAxis = 2; worstD = adrz; }
            std::printf("  d4 player rot:   az=(%d,%d,%d) soh=(%d,%d,%d) "
                        "|Δaxis0..2|=(%d,%d,%d)\n",
                        az_rx, az_ry, az_rz, soh_rx, soh_ry, soh_rz,
                        adrx, adry, adrz);
            // Tolerate ≤8 binary-angle units (rounding); anything larger is
            // a real rotation drift.
            if (worstD > 8) {
                // Post-wall-hit yaw drift is downstream of the scene-
                // collision DeferredPortTarget — inherit the d3 decision
                // when SoH is wall-touching. Also inherit rate-comp when
                // d3 was classified as rate-comp (rare — yaw rarely
                // diverges under matched-speed walk).
                if (d3_decision.cls == DivClass::DeferredPortTarget) {
                    std::printf("    d4 classified: DEFERRED (%s-downstream) — "
                                "Δ=%d wall-slide yaw\n"
                                "      origin: inherits d3 (%s)  soh=%s\n",
                                d3_decision.tag, worstD,
                                d3_decision.origin_az,
                                d3_decision.origin_soh);
                    // Player.yaw writer — Actor+0x036 was RE'd to be
                    // OoT3D's live-facing yaw slot (soh3d ec25ea2). The
                    // last write to it is the guest instruction that
                    // rotated Link this frame. Ghidra-jump → OoT3D's
                    // yaw-update path.
                    if (s_watched_yaw_addr != 0) {
                        WatchRecord y;
                        if (Soh3d_WatchGetLatestMatching(s_watched_yaw_addr,
                                0u, 0u, &y)) {
                            std::printf("      writer:  az_pc=0x%08x lr=0x%08x "
                                        "yaw=%d ticks=%lu — Ghidra-jump this PC "
                                        "for OoT3D's Player yaw update\n",
                                        y.arm_pc, y.arm_lr,
                                        (int)(short)(y.data & 0xFFFFu),
                                        (unsigned long)y.cycles);
                        }
                    }
                } else if (!fd.reported) {
                    char buf[192]; std::snprintf(buf, sizeof buf,
                        "worst axis=%d Δ=%d (az_rot=(%d,%d,%d) soh_rot=(%d,%d,%d))",
                        worstAxis, worstD, az_rx, az_ry, az_rz,
                        soh_rx, soh_ry, soh_rz);
                    fd.report("player-rot", buf);
                }
            }
        }

        // ── Play-mode d5: camera basis ─────────────────────────────────
        // Read Az mainCamera at PlayState+0x1B8 (eye,at,up as Vec3f × 3).
        // Compute dir = normalize(at - eye) so we can compare on the same
        // basis SoH exposes (SohState_Camera returns eye+at+up).
        float ex, ey, ez, ax, ay, az_at_pt, ux, uy, uz, fov;
        short roll; int camId;
        bool soh_cam = SohState_Camera(&ex, &ey, &ez, &ax, &ay, &az_at_pt,
                                       &ux, &uy, &uz, &fov, &roll, &camId) != 0;
        float az_e[3] = {0,0,0}, az_a[3] = {0,0,0}, az_u[3] = {0,0,0};
        bool az_cam_ok = true;
        for (int j = 0; j < 3; ++j) {
            auto ev = mem.Read32OrNullopt(*ps_az + PLAY_CAM_EYE_OFF + j*4);
            auto av = mem.Read32OrNullopt(*ps_az + PLAY_CAM_AT_OFF  + j*4);
            auto uv = mem.Read32OrNullopt(*ps_az + PLAY_CAM_UP_OFF  + j*4);
            if (!ev || !av || !uv) { az_cam_ok = false; break; }
            std::memcpy(&az_e[j], &*ev, 4);
            std::memcpy(&az_a[j], &*av, 4);
            std::memcpy(&az_u[j], &*uv, 4);
        }
        if (!az_cam_ok) {
            std::printf("  d5 camera basis: az=(unmapped @ ps+0x%04x) soh=%s\n",
                        PLAY_CAM_EYE_OFF, soh_cam ? "OK" : "no camera");
            if (!fd.reported) fd.report("camera-mem",
                "Azahar mainCamera bytes unreadable at ps+0x1B8");
        } else if (!soh_cam) {
            std::printf("  d5 camera basis: az_eye=(%.1f,%.1f,%.1f) "
                        "soh=(no active camera)\n",
                        az_e[0], az_e[1], az_e[2]);
            if (!fd.reported) fd.report("camera-side",
                "SoH has no active camera at scene load");
        } else {
            // Apply per-axis camera sign-flip invariant (all false today).
            const float cmx = FlipMul(AZ_CAM_X_SIGN_FLIP);
            const float cmy = FlipMul(AZ_CAM_Y_SIGN_FLIP);
            const float cmz = FlipMul(AZ_CAM_Z_SIGN_FLIP);
            const float az_e0 = az_e[0]*cmx, az_e1 = az_e[1]*cmy, az_e2 = az_e[2]*cmz;
            const float az_a0 = az_a[0]*cmx, az_a1 = az_a[1]*cmy, az_a2 = az_a[2]*cmz;
            const float az_u0 = az_u[0]*cmx, az_u1 = az_u[1]*cmy, az_u2 = az_u[2]*cmz;
            float dEye = std::sqrt((az_e0-ex)*(az_e0-ex)
                                 + (az_e1-ey)*(az_e1-ey)
                                 + (az_e2-ez)*(az_e2-ez));
            float dAt  = std::sqrt((az_a0-ax)*(az_a0-ax)
                                 + (az_a1-ay)*(az_a1-ay)
                                 + (az_a2-az_at_pt)*(az_a2-az_at_pt));
            float dUp  = std::sqrt((az_u0-ux)*(az_u0-ux)
                                 + (az_u1-uy)*(az_u1-uy)
                                 + (az_u2-uz)*(az_u2-uz));
            std::printf("  d5 camera basis: az_eye=(%.1f,%.1f,%.1f) "
                        "soh_eye=(%.1f,%.1f,%.1f) |Δeye|=%.2f "
                        "|Δat|=%.2f |Δup|=%.4f  soh_camId=%d fov=%.1f\n",
                        az_e[0], az_e[1], az_e[2], ex, ey, ez,
                        dEye, dAt, dUp, camId, fov);
            // Tolerance: 1 unit on eye/at (~2 mm at OoT world scale), 0.01 on up
            // (unit-vector rounding). Larger deltas mean actual camera drift.
            if (dEye > 1.0f || dAt > 1.0f || dUp > 0.01f) {
                // A follow-camera's at-point tracks Link. If d3 was
                // classified, d5 |Δat| divergence with matched |Δeye| is
                // downstream of the same divergence class. Only classify
                // when |Δeye| is tight (fixed-eye scenes) — real camera
                // drift moves the eye too.
                if (d3_decision.cls != DivClass::Unclassified &&
                    dEye < 1.0f && dUp < 0.01f) {
                    std::printf("    d5 classified: %s (%s-downstream) — "
                                "|Δeye|=%.2f |Δat|=%.2f — at-point tracks "
                                "Link, downstream of d3\n"
                                "      origin: inherits d3 (%s)  soh=%s\n",
                                DivClassStr(d3_decision.cls), d3_decision.tag,
                                dEye, dAt,
                                d3_decision.origin_az,
                                d3_decision.origin_soh);
                } else if (!fd.reported) {
                    char buf[192]; std::snprintf(buf, sizeof buf,
                        "|Δeye|=%.2f |Δat|=%.2f |Δup|=%.4f — camera basis drift",
                        dEye, dAt, dUp);
                    fd.report("camera-basis", buf);
                    // Auto-attach: query the most recent write to Az's eye
                    // Vec3f. mask=0 → match any write. Emits the guest ARM
                    // PC that wrote the eye — Ghidra-jump this PC for the
                    // OoT3D camera-update site (Camera_Update /
                    // Play_UpdateMainCamera / etc.).
                    if (s_watched_cam_eye_addr != 0) {
                        WatchRecord wr{};
                        if (Soh3d_WatchGetLatestMatching(
                                s_watched_cam_eye_addr, 0, 0, &wr)) {
                            std::printf("        writer: "
                                "az_pc=0x%08x lr=0x%08x eye_off=+%u "
                                "data=0x%016lx ticks=%lu — Ghidra-jump this PC "
                                "for OoT3D's camera-eye update site\n",
                                wr.arm_pc, wr.arm_lr,
                                wr.vaddr - s_watched_cam_eye_addr,
                                wr.data, (unsigned long)wr.cycles);
                        } else {
                            std::printf("        writer: (no eye writes "
                                "captured yet — advance more frames)\n");
                        }
                    }
                    // Chase mainCamera pointer at PS+0xA54, then read
                    // setting/mode/status. This is the actual Camera
                    // struct (heap-allocated), not the view-basis copy at
                    // PS+0x1B8. Expected at Kakariko:
                    //   setting=SCENE_CAM_SET_NORMAL0(=1)
                    //   mode=CAM_MODE_NORMAL(=0)
                    //   status=CAM_STAT_ACTIVE(=7)
                    auto cam_ptr = mem.Read32OrNullopt(
                        *ps_az + PLAY_CAMERAPTRS_OFF);
                    if (cam_ptr && *cam_ptr != 0) {
                        // Camera+0x188 (status u16, aligned) shares its
                        // u32 word with +0x18A (setting).
                        auto w_stset = mem.Read32OrNullopt(
                            *cam_ptr + CAMERA_STATUS_OFF);
                        auto w_mode  = mem.Read32OrNullopt(
                            *cam_ptr + CAMERA_MODE_OFF);
                        if (w_stset && w_mode) {
                            const uint16_t az_status  =
                                static_cast<uint16_t>(*w_stset);
                            const uint16_t az_setting =
                                static_cast<uint16_t>(*w_stset >> 16);
                            const uint16_t az_mode    =
                                static_cast<uint16_t>(*w_mode);
                            std::printf("        az_cam: cam@0x%08x "
                                "setting=%u mode=%u status=%u — identifies "
                                "OoT3D sCameraFunctions[funcIdx] mode "
                                "function (expect setting=1 mode=0)\n",
                                *cam_ptr,
                                (unsigned)(az_setting & 0xFF),
                                (unsigned)(az_mode    & 0xFF),
                                (unsigned)(az_status  & 0xFF));
                            // Delta B probe: when setting=2 (CAM_SET_NORMAL1)
                            // mode=0 (CAM_MODE_NORMAL), the mode function is
                            // Camera_Normal1 (OoT3D FUN_00239fd8). Its params
                            // live inline at Camera+0x00..+0x22 (Grezzo folded
                            // Camera.paramData into Camera itself — see
                            // FUN_00239fd8 field map in the handoff). Reading
                            // them here gives the fully-resolved runtime
                            // Normal1 values without needing SohState plumbing;
                            // compare against SoH's CAM_FUNCDATA_NORM1(0, 200,
                            // 400, 10, 12, 20, 40, 60, 60, 0x0003).
                            if ((az_setting & 0xFF) == 2 &&
                                (az_mode    & 0xFF) == 0) {
                                float p_yOff=0, p_dMin=0, p_dMax=0, p_u0C=0,
                                      p_u10=0, p_u14=0, p_fov=0, p_atLS=0;
                                auto rf = [&](uint32_t off, float& out) {
                                    auto v = mem.Read32OrNullopt(*cam_ptr+off);
                                    if (v) std::memcpy(&out, &*v, 4);
                                };
                                rf(0x00, p_yOff);
                                rf(0x04, p_dMin);
                                rf(0x08, p_dMax);
                                rf(0x0C, p_u0C);
                                rf(0x10, p_u10);
                                rf(0x14, p_u14);
                                rf(0x18, p_fov);
                                rf(0x1C, p_atLS);
                                auto pw =
                                    mem.Read32OrNullopt(*cam_ptr + 0x20);
                                int16_t p_pitch = pw ?
                                    static_cast<int16_t>(*pw & 0xFFFF) : 0;
                                uint16_t p_flags = pw ?
                                    static_cast<uint16_t>(*pw >> 16) : 0;
                                std::printf(
                                    "        az_norm1: "
                                    "yOff=%.2f dMin=%.1f dMax=%.1f unk_0C=%.2f "
                                    "unk_10=%.2f unk_14=%.4f fov=%.1f "
                                    "atLERP=%.4f pitchTgt=%d flags=0x%04x  "
                                    "— SoH: (0, 200, 400, 12, 20, 0.40, 60, "
                                    "0.60, 10, 0x0003)\n",
                                    p_yOff, p_dMin, p_dMax, p_u0C,
                                    p_u10, p_u14, p_fov, p_atLS,
                                    (int)p_pitch, (unsigned)p_flags);
                                // Δ-A probe: FUN_00338ac8 (Camera_CalcAtDefault)
                                // hand-disasm found an extra Y adjustment gated
                                // on *(u32*)(player + 0x29b8) & 0x100, where the
                                // adjustment magnitude is *(f32*)(player+0x1760)
                                // * -0.01f. Read both live fields to confirm
                                // the |Δeye|=25 Y drift is from this block.
                                // See oot3d-decomp docs/gameplay_firstdiv.md
                                // "Δ-A resolution".
                                auto w_player = mem.Read32OrNullopt(
                                    *cam_ptr + 0xD8);
                                if (w_player && *w_player != 0) {
                                    auto w_state = mem.Read32OrNullopt(
                                        *w_player + 0x29B8);
                                    auto w_ybias = mem.Read32OrNullopt(
                                        *w_player + 0x1760);
                                    float ybias = 0.0f;
                                    if (w_ybias) std::memcpy(
                                        &ybias, &*w_ybias, 4);
                                    uint32_t state = w_state ? *w_state : 0;
                                    bool bit100 = (state & 0x100) != 0;
                                    float extraAtY = bit100
                                        ? (ybias * -0.01f) : 0.0f;
                                    std::printf(
                                        "        az_deltaA: player=0x%08x "
                                        "state[+0x29B8]=0x%08x bit0x100=%d "
                                        "ybias[+0x1760]=%.2f "
                                        "→ extraAtY=%.2f (predicts Δat.y "
                                        "= -%.2f; observed |Δeye|~25)\n",
                                        *w_player, state, (int)bit100,
                                        ybias, extraAtY, -extraAtY);
                                    // Δ-A activation writer PC: if bit 0x100
                                    // is SET, consult the watchpoint history
                                    // for the last write to this word — the
                                    // guest PC identifies which OoT3D code
                                    // owns the state flag. Fresh info in
                                    // scenes where the block fires.
                                    if (bit100 &&
                                        s_watched_deltaA_addr != 0) {
                                        WatchRecord wr{};
                                        if (Soh3d_WatchGetLatestMatching(
                                                s_watched_deltaA_addr, 0, 0,
                                                &wr)) {
                                            std::printf(
                                                "        az_deltaA_writer: "
                                                "az_pc=0x%08x lr=0x%08x "
                                                "data=0x%016lx ticks=%lu — "
                                                "Ghidra-jump this PC to find "
                                                "OoT3D's state-flag owner\n",
                                                wr.arm_pc, wr.arm_lr,
                                                wr.data,
                                                (unsigned long)wr.cycles);
                                        }
                                    }
                                }
                                // Age/height probe: OoT3D Player_GetHeight
                                // (FUN_00367ef0) reads *(0x0058795C) as the
                                // adult/child flag; nonzero → 44 (child),
                                // zero → 68 (adult). Plus 0 or 32 from
                                // *(player+0x1710) & 0x800000. Read both;
                                // compute expected height to see if it
                                // matches the observed |Δat|=24 (== 68-44).
                                auto w_age = mem.Read32OrNullopt(0x0058795C);
                                auto w_pstate = w_player
                                    ? mem.Read32OrNullopt(*w_player + 0x1710)
                                    : std::optional<uint32_t>{};
                                float base_h = w_age
                                    ? (*w_age != 0 ? 44.0f : 68.0f) : -1.0f;
                                float adj_h = w_pstate
                                    ? ((*w_pstate & 0x800000) ? 32.0f : 0.0f)
                                    : 0.0f;
                                std::printf(
                                    "        az_height: linkAge[0x58795C]=%s "
                                    "pstate[+0x1710]=0x%08x → "
                                    "Player_GetHeight=%.1f  (SoH adult=100 "
                                    "child=68 baseline via z_player.c)\n",
                                    w_age ? (std::to_string(*w_age).c_str())
                                          : "?",
                                    w_pstate ? *w_pstate : 0u,
                                    base_h + adj_h);
                            }
                        } else {
                            std::printf("        az_cam: cam@0x%08x — "
                                "st/mode reads returned nullopt "
                                "(unmapped?)\n", *cam_ptr);
                        }
                    } else {
                        std::printf("        az_cam: cam_ptr @ ps+0x%04x "
                            "= 0 or unreadable — cameraPtrs offset guess "
                            "may be wrong\n", PLAY_CAMERAPTRS_OFF);
                    }
                }
            }
        }

        // ── Play-mode d6: total actor count and per-category deltas ────
        // Az: sum ACTOR_LISTS_OFF+cat*8+0 across all 12 categories.
        // SoH: SohState_WalkActors bins each visited actor by cat.
        int az_by_cat[12] = {0}, soh_by_cat[12] = {0};
        for (uint32_t cat = 0; cat < 12; ++cat) {
            auto cnt = mem.Read32OrNullopt(*ps_az + ACTORCTX_OFF +
                                            ACTOR_LISTS_OFF + cat * 8 + 0);
            if (cnt) az_by_cat[cat] = static_cast<int>(*cnt);
        }
        struct SohBin { int by_cat[12] = {0}; int total = 0; };
        SohBin sohBin;
        auto sink = +[](void* u, int cat, int, unsigned long, float, float, float,
                        short, short, short) {
            auto* b = static_cast<SohBin*>(u);
            if (cat >= 0 && cat < 12) b->by_cat[cat]++;
            b->total++;
        };
        SohState_WalkActors(sink, &sohBin);
        int az_total = 0;
        for (int c = 0; c < 12; ++c) az_total += az_by_cat[c];
        std::printf("  d6 actor count:  az=%d soh=%d", az_total, sohBin.total);
        // Print per-cat deltas for categories where either side is nonzero.
        char delta_str[256]; delta_str[0] = 0;
        int len = 0;
        for (int c = 0; c < 12; ++c) {
            if (az_by_cat[c] || sohBin.by_cat[c]) {
                len += std::snprintf(delta_str + len,
                    sizeof(delta_str) - len,
                    " cat%d=%d/%d", c, az_by_cat[c], sohBin.by_cat[c]);
            }
        }
        std::printf(" |%s\n", delta_str);
        if (az_total != sohBin.total) {
            // Wonder_Talk2 3DS content-add filter: total mismatch is
            // entirely explained by extra Az id=0x0185 actors. Room
            // scripts can put Wonder_Talk2 in any category (cat=7 at
            // Link's House, cat=1 has been noted elsewhere), so walk
            // EVERY cat on both sides and count id=0x0185. If (az_wt2
            // - soh_wt2) equals the total delta, classify.
            int az_wt2 = 0, soh_wt2 = 0;
            for (uint32_t cat = 0; cat < 12; ++cat) {
                auto head = mem.Read32OrNullopt(*ps_az + ACTORCTX_OFF +
                                                ACTOR_LISTS_OFF + cat * 8 + 4);
                uint32_t addr = head ? *head : 0;
                int guard = 128;
                while (addr && guard-- > 0) {
                    auto id_v = mem.Read32OrNullopt(addr + 0x00);
                    auto nx_v = mem.Read32OrNullopt(addr + ACTOR_NEXT_OFF);
                    if (!id_v || !nx_v) break;
                    if ((*id_v & 0xFFFF) == 0x0185) az_wt2++;
                    addr = *nx_v;
                }
                int soh_len = SohState_ActorListLen(cat);
                for (int i = 0; i < soh_len; ++i) {
                    int s_id=0, s_params=0; unsigned int s_flags=0;
                    float sp[3]; short sr[3];
                    if (SohState_ActorInfoAt(cat, i, &s_id, &s_params, &s_flags,
                                             &sp[0], &sp[1], &sp[2],
                                             &sr[0], &sr[1], &sr[2])
                        && s_id == 0x0185) soh_wt2++;
                }
            }
            const int total_delta = az_total - sohBin.total;
            const int wt2_delta   = az_wt2 - soh_wt2;
            const bool wt2_explains = (total_delta >= 1 && total_delta == wt2_delta);
            if (wt2_explains) {
                auto dec = ClassifyD6Content(0, 0, 0, wt2_delta);
                (void)dec;
                std::printf("    d6 classified: NOISE (wonder_talk2) — "
                            "Az has +%d id=0x0185 (3DS content add) matching "
                            "total delta\n"
                            "      origin: az=cat=*/id=0x0185 (En_Wonder_Talk2 — 3DS-only)  "
                            "soh=no equivalent  "
                            "doc=oot3d-decomp/docs/gameplay_firstdiv.md#sign-blind-policy\n",
                            wt2_delta);
            } else if (!fd.reported) {
                char buf[256]; std::snprintf(buf, sizeof buf,
                    "total az=%d soh=%d;%s — per-cat az/soh — investigate "
                    "room-load timing vs missing-actor port gap",
                    az_total, sohBin.total, delta_str);
                fd.report("actor-count", buf);
            }
        }

        // ── Play-mode d7: per-actor state diff ─────────────────────────
        // For each Az actor, find the best-matching SoH actor by
        // (cat, id, closest position). If any pair has a params mismatch
        // or a world-flags divergence, name the first one. Positions
        // already covered by d3 for Player; this dim catches:
        //  - port gap where an NPC/pickup spawns with different params
        //    (different message id, different item type, different mode);
        //  - flag drift where two actors of the same id are in different
        //    lifecycle states (e.g. one is DRAW_ENABLED, the other isn't).
        //
        // Uses the ACTOR_LISTS walking pattern (per-cat linked list) on
        // both sides, matched by (cat, id, nearest position).
        struct AzActor {
            int cat, id, params;
            uint32_t flags;
            float px, py, pz;
            short rx, ry, rz;
        };
        std::vector<AzActor> az_list;
        for (uint32_t cat = 0; cat < 12; ++cat) {
            auto head = mem.Read32OrNullopt(*ps_az + ACTORCTX_OFF +
                                              ACTOR_LISTS_OFF + cat * 8 + 4);
            if (!head || *head == 0) continue;
            uint32_t addr = *head;
            int guard = 128;
            while (addr != 0 && guard-- > 0) {
                auto id_v = mem.Read32OrNullopt(addr + 0x00);
                auto fl_v = mem.Read32OrNullopt(addr + 0x04);
                auto px_v = mem.Read32OrNullopt(addr + ACTOR_POS_OFF + 0);
                auto py_v = mem.Read32OrNullopt(addr + ACTOR_POS_OFF + 4);
                auto pz_v = mem.Read32OrNullopt(addr + ACTOR_POS_OFF + 8);
                auto rot_v= mem.Read32OrNullopt(addr + ACTOR_ROT_OFF + 0);
                auto rz_v = mem.Read32OrNullopt(addr + ACTOR_ROT_OFF + 4);
                auto pr_v = mem.Read32OrNullopt(addr + 0x1C);
                auto nx_v = mem.Read32OrNullopt(addr + ACTOR_NEXT_OFF);
                if (!id_v || !fl_v || !px_v || !py_v || !pz_v ||
                    !rot_v || !rz_v || !pr_v || !nx_v) break;
                AzActor a{};
                a.cat = static_cast<int>(cat);
                a.id  = static_cast<int>(*id_v & 0xFFFF);
                short p16 = static_cast<short>(*pr_v & 0xFFFF);
                a.params = static_cast<int>(p16);
                a.flags = *fl_v;
                std::memcpy(&a.px, &*px_v, 4);
                std::memcpy(&a.py, &*py_v, 4);
                std::memcpy(&a.pz, &*pz_v, 4);
                a.rx = static_cast<short>(*rot_v & 0xFFFF);
                a.ry = static_cast<short>((*rot_v >> 16) & 0xFFFF);
                a.rz = static_cast<short>(*rz_v & 0xFFFF);
                az_list.push_back(a);
                addr = *nx_v;
            }
        }
        // For each Az actor, try to pair with SoH's matching actor
        // (same cat, same id, nearest position). If either side has more
        // actors of that (cat,id) than the other, skip — d6 already flagged
        // the count mismatch and it's the reason for the extra.
        int checked = 0, mismatched = 0;
        std::string first_delta;
        // Track the worst per-pair position delta so time-sweeps surface
        // NPC drift even when it doesn't trip a firstdiv report.
        float worstPosD = 0.0f;
        int worstPosCat = -1, worstPosId = 0;
        for (const auto& a : az_list) {
            int soh_len = SohState_ActorListLen(a.cat);
            if (soh_len <= 0) continue;
            int best = -1;
            float bestD2 = 1e30f;
            int b_id = 0, b_params = 0; unsigned int b_flags = 0;
            float b_px = 0, b_py = 0, b_pz = 0;
            short b_rx = 0, b_ry = 0, b_rz = 0;
            for (int i = 0; i < soh_len; ++i) {
                int s_id=0, s_params=0; unsigned int s_flags=0;
                float s_px=0, s_py=0, s_pz=0;
                short s_rx=0, s_ry=0, s_rz=0;
                if (!SohState_ActorInfoAt(a.cat, i, &s_id, &s_params, &s_flags,
                                          &s_px, &s_py, &s_pz,
                                          &s_rx, &s_ry, &s_rz)) continue;
                if (s_id != a.id) continue;
                // Apply per-axis position sign-flip invariant to the Az side
                // before differencing. All flags false today.
                const float a_px = a.px * FlipMul(AZ_POS_X_SIGN_FLIP);
                const float a_py = a.py * FlipMul(AZ_POS_Y_SIGN_FLIP);
                const float a_pz = a.pz * FlipMul(AZ_POS_Z_SIGN_FLIP);
                float dx = s_px - a_px, dy = s_py - a_py, dz = s_pz - a_pz;
                float d2 = dx*dx + dy*dy + dz*dz;
                if (d2 < bestD2) {
                    bestD2 = d2; best = i;
                    b_id = s_id; b_params = s_params; b_flags = s_flags;
                    b_px = s_px; b_py = s_py; b_pz = s_pz;
                    b_rx = s_rx; b_ry = s_ry; b_rz = s_rz;
                }
            }
            // Position-proximity gate: an Az actor is considered "paired"
            // with a SoH actor only when the same-cat/same-id nearest is
            // within a generous threshold. Otherwise d7 would fabricate a
            // fake pair (e.g. Az's Wonder_Talk2 at (0,20,120) matched
            // against SoH's Wonder_Talk2 at (78,38,116) because they
            // share id 0x0185) and mis-report a params mismatch that d6
            // already flagged as a missing actor. Threshold: 40 units
            // — one Link-height, generous enough for slow-moving NPCs
            // between the two engine step points, tight enough that
            // cross-room duplicates don't collide.
            const float PAIR_DIST2 = 40.0f * 40.0f;
            if (best < 0 || bestD2 > PAIR_DIST2) continue;
            checked++;
            float posD = std::sqrt(bestD2);
            if (posD > worstPosD) {
                worstPosD = posD; worstPosCat = a.cat; worstPosId = a.id;
            }
            // Params must match exactly (they're the spawn-data seed).
            if (b_params != a.params) {
                mismatched++;
                if (first_delta.empty()) {
                    char buf[256]; std::snprintf(buf, sizeof buf,
                        "cat=%d id=0x%04x az_params=0x%04x soh_params=0x%04x "
                        "az_pos=(%.1f,%.1f,%.1f) soh_pos=(%.1f,%.1f,%.1f)",
                        a.cat, a.id, a.params & 0xFFFF, b_params & 0xFFFF,
                        a.px, a.py, a.pz, b_px, b_py, b_pz);
                    first_delta = buf;
                }
                continue;
            }
            // Flags: bits >= 0x10 are lifecycle/behavior; the low bits
            // (0x01..0x08) toggle every frame and are noisy. Mask them.
            uint32_t azf = a.flags & 0xFFFFFFF0u;
            uint32_t sof = b_flags & 0xFFFFFFF0u;
            if (azf != sof && first_delta.empty()) {
                mismatched++;
                char buf[256]; std::snprintf(buf, sizeof buf,
                    "cat=%d id=0x%04x az_flags=0x%08x soh_flags=0x%08x "
                    "(masked hi bits) at az_pos=(%.1f,%.1f,%.1f)",
                    a.cat, a.id, azf, sof, a.px, a.py, a.pz);
                first_delta = "flags: " + std::string(buf);
            }
        }
        std::printf("  d7 actor pairs:  checked=%d mismatched=%d",
                    checked, mismatched);
        if (mismatched > 0) {
            std::printf("  first=%s\n", first_delta.c_str());
            if (!fd.reported) fd.report("actor-state", first_delta);
        } else {
            std::printf(" worst_pos_drift=%.2f (cat=%d id=0x%04x)\n",
                        worstPosD, worstPosCat, worstPosId);
            // Filter Navi RNG (cat=7 id=0x0018) — permanent noise. Not
            // that d7 fires a report on worst_pos_drift; but printing the
            // classification tag documents the policy at the sweep site.
            auto dec = ClassifyD7Worst(worstPosCat, worstPosId);
            if (dec.cls != DivClass::Unclassified && worstPosD > 4.0f) {
                std::printf("    d7 classified: %s (%s) — cat=%d id=0x%04x drift=%.2f\n"
                            "      origin: az=%s  soh=%s  doc=%s\n",
                            DivClassStr(dec.cls), dec.tag,
                            worstPosCat, worstPosId, worstPosD,
                            dec.origin_az, dec.origin_soh, dec.origin_doc);
            }
        }

        if (!fd.reported) {
            std::printf("  firstdiv: none — all 7 play-mode dimensions matched\n");
        }
        return;
    }
    // Fall through to title-mode d3..d5 below.

    // Dim 3: Link ↔ Link limb count under the RE'd index mapping.
    //
    // Table B (0x005A54D8) has 25 entries but only 22 are "SkelAnime-shaped":
    //   entry 0    = zero-filled default (unused root-parent slot)
    //   entry 1    = root translation  (matches SoH jointTable[0])
    //   entries 2..22 = 21 limb rotations (match SoH jointTable[1..21])
    //   entries 23, 24 = trailing zeros (OoT3D-only extras — facial or
    //                    finger joints the N64 rig doesn't have)
    //
    // So table B IS a 22-limb Link rig with 3 OoT3D-specific extras. The
    // port-native mapping is `3ds_idx = soh_idx + 1`; d3 counts under this
    // mapping should match. This preserves SoH's animation-resolver
    // invariants (Link's SkelAnime stays 22 limbs; the compare tool learns
    // the layout, not the engine).
    constexpr int AZ_LIMB_OFFSET   = 1;   // 3ds_idx = soh_idx + AZ_LIMB_OFFSET
    constexpr int AZ_EXTRAS_TRAIL  = 2;   // entries 23, 24 skipped
    const int az_link_limbs_raw = az_at_title ? (int)TITLE_POSE_B_COUNT : 0;
    const int az_link_limbs_mapped = az_at_title
        ? az_link_limbs_raw - AZ_LIMB_OFFSET - AZ_EXTRAS_TRAIL  // 25 - 1 - 2 = 22
        : 0;
    int soh_link_limbs = 0;
    short soh_joints[32 * 3] = {};
    int animFrame = 0, morphFrame = 0;
    if (soh_at_title) {
        int n = SohState_ActorSkeleton(2, 0, soh_joints, 32,
                                       &soh_link_limbs, &animFrame, &morphFrame);
        if (n < 0) soh_link_limbs = -1;
    }
    std::printf("  d3 link limbs:   az(table B mapped)=%d "
                "(raw=%d, drop entry0 + 2 trailing extras) soh(Player)=%d\n",
                az_link_limbs_mapped, az_link_limbs_raw, soh_link_limbs);
    if (!fd.reported && az_link_limbs_mapped != soh_link_limbs) {
        char buf[192]; std::snprintf(buf, sizeof buf,
            "az(table B mapped)=%d soh(gPlayer skelAnime)=%d "
            "— mapping needs tuning",
            az_link_limbs_mapped, soh_link_limbs);
        fd.report("link-limb-count", buf);
    }

    // Dim 4: per-limb rotation delta under the mapping. SoH jointTable[0]
    // stores ROOT TRANSLATION as Vec3s (not a rotation), so skip index 0
    // on the rotation-delta check — its natural pair is 3DS entry[1].pos.
    // Rotations are at SoH jointTable[1..21] ↔ 3DS entry[2..22].rot.
    if (!fd.reported && az_link_limbs_mapped > 0 &&
        az_link_limbs_mapped == soh_link_limbs) {
        int worstLimb = -1, worstAxis = -1;
        int worstDelta = 0;
        short worstAz = 0, worstSoh = 0;
        // Also tally delta statistics per axis so we can see if the
        // divergence is uniform (sign flip / axis convention) or random
        // (demos out of phase).
        long long sumAbsPerAxis[3] = {0, 0, 0};
        int hitsPerAxis[3] = {0, 0, 0};
        int nearHalfTurnCount = 0;  // deltas within 0x0400 of ±0x8000
        for (int soh_i = 1; soh_i < soh_link_limbs; ++soh_i) {
            const int az_i = soh_i + AZ_LIMB_OFFSET;
            const uint32_t va = TITLE_POSE_TABLE_B_VA + az_i * TITLE_POSE_STRIDE;
            float rad[3];
            bool ok = true;
            for (int j = 0; j < 3; ++j) {
                auto v = mem.Read32OrNullopt(va + 12 + j * 4);
                if (!v) { ok = false; break; }
                std::memcpy(&rad[j], &*v, 4);
            }
            if (!ok) continue;
            for (int j = 0; j < 3; ++j) {
                short az_bin  = RadToBinaryAngle(rad[j]);
                short soh_bin = soh_joints[soh_i * 3 + j];
                int d = az_bin - soh_bin;
                if (d >  32768) d -= 65536;
                if (d < -32768) d += 65536;
                int a = d < 0 ? -d : d;
                sumAbsPerAxis[j] += a;
                hitsPerAxis[j]++;
                // Half-turn signature: |a - 0x8000| < 0x0400
                if (a > 0x7c00 && a < 0x8400) nearHalfTurnCount++;
                if (a > worstDelta) {
                    worstDelta = a;
                    worstLimb = soh_i; worstAxis = j;
                    worstAz = az_bin; worstSoh = soh_bin;
                }
            }
        }
        std::printf("  d4 rot delta:    worst=%d (soh limb %d axis %d, "
                    "az entry %d) az_bin=%d soh_bin=%d\n",
                    worstDelta, worstLimb, worstAxis,
                    worstLimb + AZ_LIMB_OFFSET, worstAz, worstSoh);
        std::printf("  d4 mean |Δ|:     axis0=%lld axis1=%lld axis2=%lld  "
                    "half-turn hits=%d/%d\n",
                    hitsPerAxis[0] ? sumAbsPerAxis[0]/hitsPerAxis[0] : 0,
                    hitsPerAxis[1] ? sumAbsPerAxis[1]/hitsPerAxis[1] : 0,
                    hitsPerAxis[2] ? sumAbsPerAxis[2]/hitsPerAxis[2] : 0,
                    nearHalfTurnCount,
                    hitsPerAxis[0] + hitsPerAxis[1] + hitsPerAxis[2]);
        if (worstDelta > 0x0800) {
            char buf[256]; std::snprintf(buf, sizeof buf,
                "worst soh limb %d axis %d Δ=%d (az=%d soh=%d) "
                "— see d4 mean|Δ| line for uniform-vs-random signature",
                worstLimb, worstAxis, worstDelta, worstAz, worstSoh);
            fd.report("link-limb-rot", buf);
        }
    }

    // title-actor: real cross-engine |Δpos| between Az's title-demo rider
    // world.pos (RE'd 0x005AFFB0 — see oot3d-decomp z_title_demo.c and
    // docs/title_actor_world_pos.md) and SoH's Player.world.pos.
    //
    // The prior version dumped Table B entry 1 (limb-local mount-attachment
    // offset y≈7875) alongside SoH's world.pos as a DIAGNOSTIC only, with
    // no |Δ| computed because it would be nonsense (limb-local vs world).
    // Now that Az's actual rider WORLD.pos slot is RE'd, this emits a real
    // metric. Firstdiv threshold picked at 500u — Hyrule Field flyover
    // motion is ~3.9 units/frame; a few-second desync would exceed 500u.
    if (az_at_title && soh_at_title) {
        Az3dsTitleLinkWorldPos az_link{};
        const bool az_ok = Az_ReadTitleLinkPos(&az_link);
        float spx=0, spy=0, spz=0;
        short srx=0, sry=0, srz=0;
        const bool soh_ok = SohState_PlayerPos(&spx, &spy, &spz,
                                               &srx, &sry, &srz) != 0;
        if (az_ok && soh_ok) {
            const float dx = az_link.pos[0] - spx;
            const float dy = az_link.pos[1] - spy;
            const float dz = az_link.pos[2] - spz;
            const float d  = std::sqrt(dx*dx + dy*dy + dz*dz);
            std::printf("  title-actor: az_world=(%.1f,%.1f,%.1f)  "
                        "soh_world=(%.1f,%.1f,%.1f)  |Δ|=%.1f\n",
                        az_link.pos[0], az_link.pos[1], az_link.pos[2],
                        spx, spy, spz, d);
            if (d > 500.0f) {
                char buf[192];
                std::snprintf(buf, sizeof(buf),
                    "|Δ|=%.1fu  az=(%.1f,%.1f,%.1f) soh=(%.1f,%.1f,%.1f)",
                    d, az_link.pos[0], az_link.pos[1], az_link.pos[2],
                    spx, spy, spz);
                fd.report("title-actor", buf);
            }
        } else if (az_ok && !soh_ok) {
            std::printf("  title-actor: az_world=(%.1f,%.1f,%.1f)  "
                        "soh=(no Player actor live)\n",
                        az_link.pos[0], az_link.pos[1], az_link.pos[2]);
        } else if (!az_ok && soh_ok) {
            std::printf("  title-actor: az=(unmapped 0x%08x)  "
                        "soh_world=(%.1f,%.1f,%.1f)\n",
                        TITLE_LINK_WORLD_POS_VA, spx, spy, spz);
        } else {
            std::printf("  title-actor: az=(unmapped) soh=(no Player)\n");
        }
    } else if (az_at_title) {
        std::printf("  title-actor: az_at_title=yes soh_at_title=no\n");
    }

    // Dim 5: camera basis. RE'd by find_cam_eye.py — the 3DS title-demo
    // camera is stored as eye + dir(unit) + up(unit) at 0x005BE6D4/+12/+24.
    // SoH stores eye + at + up; convert with at ≈ eye + dir*|eye→at_soh|
    // for a comparable magnitude, but the invariant we care about is
    // eye position ≈ eye and dir ≈ normalize(at-eye).
    if (az_at_title) {
        Az3dsTitleCameraBasis basis{};
        const bool ok = Az_ReadTitleCameraBasis(&basis);
        float* az_eye = basis.eye;
        float* az_dir = basis.dir;
        float* az_up  = basis.up;
        if (ok && soh_at_title) {
            float ex, ey, ez, ax, ay, az_at, ux, uy, uz, fov;
            short roll; int camId;
            if (SohState_Camera(&ex, &ey, &ez, &ax, &ay, &az_at,
                                &ux, &uy, &uz, &fov, &roll, &camId)) {
                float soh_dir[3] = { ax-ex, ay-ey, az_at-ez };
                float m = std::sqrt(soh_dir[0]*soh_dir[0]+soh_dir[1]*soh_dir[1]
                                  + soh_dir[2]*soh_dir[2]);
                if (m > 1e-4f) { soh_dir[0]/=m; soh_dir[1]/=m; soh_dir[2]/=m; }
                float dEye  = std::sqrt((az_eye[0]-ex)*(az_eye[0]-ex)
                                      + (az_eye[1]-ey)*(az_eye[1]-ey)
                                      + (az_eye[2]-ez)*(az_eye[2]-ez));
                float dDir  = std::sqrt((az_dir[0]-soh_dir[0])*(az_dir[0]-soh_dir[0])
                                      + (az_dir[1]-soh_dir[1])*(az_dir[1]-soh_dir[1])
                                      + (az_dir[2]-soh_dir[2])*(az_dir[2]-soh_dir[2]));
                float dUp   = std::sqrt((az_up[0]-ux)*(az_up[0]-ux)
                                      + (az_up[1]-uy)*(az_up[1]-uy)
                                      + (az_up[2]-uz)*(az_up[2]-uz));
                // Distinct "title-cam:" tag (not "d5 camera basis:") — makes
                // firstdiv output separately grep-able from actor-drift
                // channels. Post-17221301 the expected residual is ~2u eye
                // + <0.001 dir/up (float precision from the eye→at
                // synthesis). Anything larger is a real regression.
                std::printf("  title-cam: az_eye=(%.1f,%.1f,%.1f) "
                            "soh_eye=(%.1f,%.1f,%.1f) |Δeye|=%.2f "
                            "|Δdir|=%.4f |Δup|=%.4f  (expect <5u post-port)\n",
                            az_eye[0], az_eye[1], az_eye[2],
                            ex, ey, ez, dEye, dDir, dUp);
                // TASK16 landscape/moon-position probe. Both engines nominally
                // share the same eye + dir + up, yet render different terrain
                // (SoH shows Lon-Lon area, Az shows Death Mountain) and the
                // moon opcode SoH emits lands ~66° off-frame. Suspect the
                // projection (FOV) differs. Print SoH fov + dump 32 words of
                // 3DS mem just after the RE'd basis so we can eyeball a
                // plausible fov slot (deg range 30..90, rad range 0.5..1.6).
                std::printf("  title-cam:fov soh=%.2f°  activeCamId=%d\n",
                            fov, camId);
                std::printf("  title-cam:probe @ 0x%08x+0x24..0x84 (post-basis):",
                            TITLE_CAM_BASIS_VA);
                for (int off = 0x24; off <= 0x84; off += 4) {
                    auto w = Core::System::GetInstance().Memory()
                                 .Read32OrNullopt(TITLE_CAM_BASIS_VA + off);
                    if (!w) { std::printf(" ??"); continue; }
                    float f; uint32_t u = *w; std::memcpy(&f, &u, 4);
                    std::printf(" +%02x=%.3f", off, f);
                }
                std::printf("\n");
                // TASK16: OoT3D FUN_002d9e68 (LookAt) is LEFT-HANDED —
                // forward = normalize(eye-at), right = up×forward. Az
                // stored right at +0x140 = (-0.868, 0.195, 0.458). Print
                // Az's LH right and the RH-flipped right SoH would need
                // to match, alongside SoH's view frame (fov + eye/up
                // above). If SoH's view matrix construction still doesn't
                // match, the residual is projection/z/clip axis, not
                // eye/dir/up.
                {
                    auto rd = [&](uint32_t va)->std::optional<float>{
                        auto w = Core::System::GetInstance().Memory().Read32OrNullopt(va);
                        if (!w) return std::nullopt;
                        float f; uint32_t u=*w; std::memcpy(&f,&u,4); return f;
                    };
                    float dx=az_dir[0], dy=az_dir[1], dz=az_dir[2];
                    float ux2=az_up[0], uy2=az_up[1], uz2=az_up[2];
                    // LH: right = up × forward
                    float rlx = uy2*dz - uz2*dy;
                    float rly = uz2*dx - ux2*dz;
                    float rlz = ux2*dy - uy2*dx;
                    std::printf("  title-cam:LH-right derived (up × dir) = (%.3f,%.3f,%.3f)\n",
                                rlx, rly, rlz);
                    auto rx = rd(TITLE_CAM_BASIS_VA + 0x24);
                    auto ry = rd(TITLE_CAM_BASIS_VA + 0x28);
                    auto rz = rd(TITLE_CAM_BASIS_VA + 0x2c);
                    if (rx && ry && rz) {
                        std::printf("  title-cam:stored right @ +0x140 = (%.3f,%.3f,%.3f)  "
                                    "(match LH → OoT3D uses LH view basis)\n",
                                    *rx, *ry, *rz);
                    }
                }
                if (!fd.reported && dEye > 200.0f) {
                    char buf[192]; std::snprintf(buf, sizeof buf,
                        "|Δeye|=%.1f (az=%.0f,%.0f,%.0f soh=%.0f,%.0f,%.0f)",
                        dEye, az_eye[0], az_eye[1], az_eye[2], ex, ey, ez);
                    fd.report("title-cam", buf);
                }
            } else {
                std::printf("  title-cam: az_eye=(%.1f,%.1f,%.1f) "
                            "soh=(no active camera)\n",
                            az_eye[0], az_eye[1], az_eye[2]);
            }
        } else if (!ok) {
            std::printf("  title-cam: az=(unmapped) soh=?\n");
        } else {
            std::printf("  title-cam: az_eye=(%.1f,%.1f,%.1f) soh=(no playstate)\n",
                        az_eye[0], az_eye[1], az_eye[2]);
        }
    } else {
        std::printf("  title-cam: az=not-at-title\n");
    }

    if (!fd.reported) {
        std::printf("  firstdiv: none — all 5 checked dimensions matched\n");
    }
}
// ─────────────────────────────────────────────────────────────────────────────

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
    if (sub == "titletime") {
        // Sync anchor for the title-demo cursors: write N to
        //   OoT3D 0x0054CC3C (u32) — the +1/frame counter found by the
        //     runtime dump-diff scan; RE'd writers are FUN_004175d4 (reset
        //     via *iVar1+8 store) + register-indexed increment path.
        //   SoH   gPlayState->csCtx.frames (u16) — the game's cutscene
        //     frame counter for the title-demo cutscene playing at
        //     SCENE_HYRULE_FIELD.
        // Both engines evaluate keyframe interpolation off their own
        // cursor, so seeding them to the same N puts the pose eval at
        // the same phase — expected to collapse d4's out-of-phase drift.
        std::string n_s;
        if (!(toks >> n_s)) { PrintErr("force titletime: usage: force titletime <N>"); return; }
        auto n = ParseNum(n_s);
        if (!n) { PrintErr("force titletime: bad N"); return; }
        const uint32_t v = static_cast<uint32_t>(*n & 0xFFFFFFFFu);
        auto& mem = Core::System::GetInstance().Memory();
        mem.Write32(0x0054CC3C, v);
        int soh_ok = SohState_SetCsFrames(static_cast<int>(v & 0xFFFF));
        std::printf("ok force titletime %u  az_write=0x0054CC3C soh_write=%s\n",
                    (unsigned)v, soh_ok ? "csCtx.frames" : "err(no playstate)");
        return;
    }
    if (sub == "titletime_read") {
        auto& mem = Core::System::GetInstance().Memory();
        auto az = mem.Read32OrNullopt(0x0054CC3C);
        int soh = SohState_HasPlayState() ? SohState_CsFrames() : -1;
        std::printf("ok force titletime_read\n"
                    "  az=0x0054CC3C: %s\n"
                    "  soh csCtx.frames: %d\n"
                    "ok end\n",
                    az ? std::to_string(*az).c_str() : "unmapped", soh);
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
    else if (sub == "firstdiv")    CompareFirstDivImpl();
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
        else if (cmd == "draw_log") {
            // draw_log <path>  → open + enable one-frame draw log
            // draw_log off     → disable
            std::string arg; toks >> arg;
            if (arg == "off" || arg.empty()) {
                soh3d_draw_log_active = 0;
                std::printf("ok draw_log off\n");
            } else {
                std::snprintf(soh3d_draw_log_path, sizeof soh3d_draw_log_path, "%s", arg.c_str());
                std::FILE* f = std::fopen(arg.c_str(), "w");
                if (f) std::fclose(f);  // truncate
                soh3d_draw_log_active = 1;
                std::printf("ok draw_log %s\n", arg.c_str());
            }
        }
        else if (cmd == "az_ticks") {
            // Emu-tick counter — the DETERMINISTIC substrate under `run`.
            // `run <N>` calls retro_run N times, but each retro_run advances a
            // variable slice depending on host wall-clock scheduling → different
            // stop-ticks across sessions. Use `az_ticks` + `az_run_until` for
            // session-repeatable parity work (see task #11 in soh3d/BACKLOG).
            const auto ticks = Core::System::GetInstance().CoreTiming().GetGlobalTicks();
            std::printf("ok az_ticks %lld\n", static_cast<long long>(ticks));
        }
        else if (cmd == "az_run_until") {
            std::string n_s;
            if (!(toks >> n_s)) { PrintErr("az_run_until: usage: az_run_until <ticks>"); continue; }
            auto target = ParseNum(n_s);
            if (!target) { PrintErr("az_run_until: bad ticks"); continue; }
            // Advance one retro_run at a time until we reach the target. May
            // OVERSHOOT (retro_run's slice granularity is variable and >1); the
            // overshoot is bounded by one video frame ≈ 5.6M ARM11 ticks. For
            // sub-frame determinism, use `run 1` in a Python loop that watches
            // az_ticks and calls SingleStep for the tail. That's overkill for
            // parity work — anchoring to a fixed target-tick within ~one video
            // frame is enough to eliminate the current session-drift problem.
            auto& sys = Core::System::GetInstance();
            uint64_t frames = 0;
            const int kMaxFrames = 100000;  // safety cap; kills runaway
            while (sys.CoreTiming().GetGlobalTicks() < static_cast<s64>(*target)
                   && frames < kMaxFrames && !g_quit_requested.load()) {
                { FrameWatchdog wd("az_run_until/retro_run"); retro_run(); }
                ++frames;
            }
            const auto final_ticks = sys.CoreTiming().GetGlobalTicks();
            std::printf("ok az_run_until frames=%llu final_ticks=%lld target=%lld\n",
                        static_cast<unsigned long long>(frames),
                        static_cast<long long>(final_ticks),
                        static_cast<long long>(*target));
        }
        else if (cmd == "r8")        HandleRead(toks, 8);
        else if (cmd == "r16")       HandleRead(toks, 16);
        else if (cmd == "r32")       HandleRead(toks, 32);
        else if (cmd == "w8")        HandleWrite(toks, 8);
        else if (cmd == "w16")       HandleWrite(toks, 16);
        else if (cmd == "w32")       HandleWrite(toks, 32);
        else if (cmd == "mem")       HandleMem(toks);
        else if (cmd == "memscan")   HandleMemScan(toks);
        else if (cmd == "dumprange") HandleDumpRange(toks);
        else if (cmd == "dumpphys")  HandleDumpPhys(toks);
        else if (cmd == "input")     HandleInput(toks);
        else if (cmd == "diag")      HandleDiag(toks);
        else if (cmd == "loadstate") HandleLoadState(toks);
        else if (cmd == "savestate") HandleSaveState(toks);
        else if (cmd == "soh_z3dlive") {
            // Structured dump of the live zelda3d gl scene light state — what
            // the fragment shader sees after Zelda3D_GL_SetLightParams.
            float amb[3] = {0}, l1[3] = {0}, l2[3] = {0};
            SohState_Zelda3DLive(amb, l1, l2);
            std::printf("ok soh_z3dlive ambient=(%.3f,%.3f,%.3f) "
                        "light1Col=(%.3f,%.3f,%.3f) light2Col=(%.3f,%.3f,%.3f)\n",
                        amb[0], amb[1], amb[2], l1[0], l1[1], l1[2], l2[0], l2[1], l2[2]);
        }
        else if (cmd == "soh_titlecs") {
            // Get/set SoH's ported title-cs frame cursor (zelda3d_cutscene.cpp).
            // Usage: soh_titlecs [frame] — pin to Az's csCtx frame for lockstep A/B.
            std::string f_s;
            if (toks >> f_s) {
                auto f = ParseNum(f_s);
                if (!f) { PrintErr("soh_titlecs: bad frame"); continue; }
                Zelda3D_TitleCsSetFrame(static_cast<int>(*f));
            }
            std::printf("ok soh_titlecs frame=%d end=%d\n",
                        Zelda3D_TitleCsFrame(), Zelda3D_TitleCsEndFrame());
        }
        else if (cmd == "soh_letterbox") {
            std::printf("ok soh_letterbox %d\n", SohState_ShrinkWindowVal());
        }
        else if (cmd == "soh_env") {
            // Structured env dump: SoH-side live daytime, skybox indices,
            // and lightCtx ambient/fog. Used by parity probes.
            unsigned int daytime = 0;
            unsigned char sk1 = 0, sk2 = 0;
            float blend = 0;
            unsigned char amb[3] = {0}, fog[3] = {0};
            short fn = 0, ff = 0;
            if (!SohState_DayTimeAndEnv(&daytime, &sk1, &sk2, &blend, amb, fog, &fn, &ff)) {
                PrintErr("soh_env: no playstate"); continue;
            }
            std::printf("ok soh_env daytime=0x%04x skybox1=%u skybox2=%u blend=%.3f "
                        "ambient=(%u,%u,%u) fog=(%u,%u,%u) fogNear=%d fogFar=%d\n",
                        daytime, sk1, sk2, blend,
                        amb[0], amb[1], amb[2],
                        fog[0], fog[1], fog[2], fn, ff);
        }
        else if (cmd == "soh_moon") {
            // TEMPORARY (item A, #146 moon-scale derivation): print SoH's
            // live envCtx.sunPos.y and the moon color/scale/discScale it
            // implies, so a fixed replacement constant can be oracle-
            // anchored instead of guessed. Remove once the fixed scale lands.
            float sunPosY = 0, color = 0, scale = 0, discScale = 0;
            if (!SohState_MoonDebug(&sunPosY, &color, &scale, &discScale)) {
                PrintErr("soh_moon: no playstate"); continue;
            }
            std::printf("ok soh_moon sunPosY=%.4f color=%.4f scale=%.4f discScale=%.4f\n",
                        sunPosY, color, scale, discScale);
        }
        else if (cmd == "az_daytime") {
            // Read gSaveContext.dayTime straight from the fixed .bss VA —
            // works during title (gPlayState==0) since gSaveContext is a
            // global, not play-relative. u16 lives at offset 0x0C; the
            // word read covers 0x0C..0x0F, dayTime is the low 16 bits
            // (little-endian ARM).
            auto& mem = Core::System::GetInstance().Memory();
            auto w = mem.Read32OrNullopt(GSAVECONTEXT_VA + SAVECONTEXT_DAYTIME_OFF);
            if (!w) { PrintErr("az_daytime: unmapped"); continue; }
            unsigned dayTime = (*w) & 0xFFFFu;
            std::printf("ok az_daytime daytime=0x%04x\n", dayTime);
        }
        else if (cmd == "playstate") HandlePlayState(toks);
        else if (cmd == "titleactors") HandleTitleActors(toks);
        else if (cmd == "scene")     HandleScene(toks);
        else if (cmd == "warp")      HandleWarp(toks);
        else if (cmd == "soh_warp")  HandleSohWarp(toks);
        else if (cmd == "soh_setage") HandleSohSetAge(toks);
        else if (cmd == "soh_getage") HandleSohGetAge(toks);
        else if (cmd == "soh_ctlflags") {
            if (!g_soh_booted) { PrintErr("soh_ctlflags: run soh_boot first"); continue; }
            unsigned int sf1=0, csI=0, nCsI=0;
            int csS=0, tt=0, csA=0;
            if (!SohState_DumpControlFlags(&sf1, &csS, &csI, &nCsI, &tt, &csA)) {
                PrintErr("soh_ctlflags: no playstate"); continue;
            }
            std::printf("ok stateFlags1=0x%08x csState=%d cutsceneIndex=0x%04x "
                        "nextCsIndex=0x%04x transitionTrigger=%d csAction=%d\n",
                        sf1, csS, csI, nCsI, tt, csA);
        }
        else if (cmd == "analog") {
            std::string lx_s, ly_s, rx_s, ry_s;
            if (!(toks >> lx_s) || !(toks >> ly_s)) {
                PrintErr("analog: usage: analog <lx> <ly> [rx] [ry]  (s16 range -32768..32767)");
                continue;
            }
            auto lx = ParseNum(lx_s), ly = ParseNum(ly_s);
            if (!lx || !ly) { PrintErr("analog: bad number"); continue; }
            g_az_analog_lx = static_cast<int16_t>(*lx);
            g_az_analog_ly = static_cast<int16_t>(*ly);
            if (toks >> rx_s) { auto v = ParseNum(rx_s); if (v) g_az_analog_rx = static_cast<int16_t>(*v); }
            if (toks >> ry_s) { auto v = ParseNum(ry_s); if (v) g_az_analog_ry = static_cast<int16_t>(*v); }
            std::printf("ok analog L=(%d,%d) R=(%d,%d)\n",
                        (int)g_az_analog_lx, (int)g_az_analog_ly,
                        (int)g_az_analog_rx, (int)g_az_analog_ry);
        }
        else if (cmd == "watch") {
            std::string as, ss;
            if (!(toks >> as)) { PrintErr("watch: usage: watch <addr> [size]"); continue; }
            auto a = ParseNum(as);
            if (!a) { PrintErr("watch: bad addr"); continue; }
            uint32_t size = 4;
            if (toks >> ss) { auto v = ParseNum(ss); if (v) size = (uint32_t)*v; }
            Soh3d_WatchAddRange((uint32_t)*a, size);
            std::printf("ok watch 0x%08x %u\n", (unsigned)*a, size);
        }
        else if (cmd == "unwatch") {
            std::string as, ss;
            if (!(toks >> as)) { PrintErr("unwatch: usage: unwatch <addr> [size]"); continue; }
            auto a = ParseNum(as);
            if (!a) { PrintErr("unwatch: bad addr"); continue; }
            uint32_t size = 4;
            if (toks >> ss) { auto v = ParseNum(ss); if (v) size = (uint32_t)*v; }
            Soh3d_WatchRemoveRange((uint32_t)*a, size);
            std::printf("ok unwatch 0x%08x %u\n", (unsigned)*a, size);
        }
        else if (cmd == "watches") {
            WatchRange rs[32];
            std::size_t n = Soh3d_WatchListRanges(rs, 32);
            std::printf("ok watches %zu\n", n);
            for (std::size_t i = 0; i < n; ++i) {
                std::printf("  0x%08x %u\n", rs[i].addr, rs[i].size);
            }
            std::printf("ok end\n");
        }
        else if (cmd == "hits") {
            std::string as;
            if (!(toks >> as)) { PrintErr("hits: usage: hits <watch_base_addr>"); continue; }
            auto a = ParseNum(as);
            if (!a) { PrintErr("hits: bad addr"); continue; }
            WatchRecord recs[128];
            std::size_t n = Soh3d_WatchGetHits((uint32_t)*a, recs, 128);
            std::printf("ok hits %zu\n", n);
            for (std::size_t i = 0; i < n; ++i) {
                std::printf("  vaddr=0x%08x size=%u data=0x%016lx "
                            "pc=0x%08x lr=0x%08x ticks=%lu "
                            "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x sp=0x%08x\n",
                            recs[i].vaddr, recs[i].size,
                            (unsigned long)recs[i].data,
                            recs[i].arm_pc, recs[i].arm_lr,
                            (unsigned long)recs[i].cycles,
                            recs[i].arm_r0, recs[i].arm_r1,
                            recs[i].arm_r2, recs[i].arm_r3,
                            recs[i].arm_sp);
                std::printf("    stack:");
                for (int j = 0; j < 256; ++j) {
                    std::printf(" %08x", recs[i].stack_words[j]);
                }
                std::printf("\n");
            }
            std::printf("ok end\n");
        }
        else if (cmd == "hitclear") {
            std::string as;
            uint32_t a = 0;
            if (toks >> as) { auto v = ParseNum(as); if (v) a = (uint32_t)*v; }
            Soh3d_WatchClear(a);
            std::printf("ok hitclear 0x%08x\n", a);
        }
        else if (cmd == "az_playerinfo") {
            // Read Az Player speedXZ from the discovered offset. Walks the
            // cat=2 (Player) actor list head to find the Player, then
            // reads +0x068 as f32.
            auto ps = CurrentPlayState();
            if (!ps) { PrintErr("az_playerinfo: no playstate"); continue; }
            auto& mem = Core::System::GetInstance().Memory();
            auto head = mem.Read32OrNullopt(*ps + ACTORCTX_OFF +
                                            ACTOR_LISTS_OFF + 2 * 8 + 4);
            if (!head || *head == 0) {
                PrintErr("az_playerinfo: no Player actor"); continue;
            }
            auto spd_v = mem.Read32OrNullopt(*head + ACTOR_SPEEDXZ_OFF);
            auto rot_v = mem.Read32OrNullopt(*head + ACTOR_ROT_OFF);
            // PLAYER_YAW_OFF is an s16; Read16 not available so read
            // the enclosing u32 and mask.
            auto yaw_v = mem.Read32OrNullopt(*head + (PLAYER_YAW_OFF & ~3u));
            if (!spd_v || !rot_v || !yaw_v) {
                PrintErr("az_playerinfo: mem read fail"); continue;
            }
            float speedXZ;
            std::memcpy(&speedXZ, &*spd_v, 4);
            short rx = static_cast<short>(*rot_v & 0xFFFF);
            short ry = static_cast<short>((*rot_v >> 16) & 0xFFFF);
            // PLAYER_YAW_OFF = 0x36 sits in the high half of the u32 at 0x34.
            short playerYaw = static_cast<short>(
                (*yaw_v >> ((PLAYER_YAW_OFF & 2) * 8)) & 0xFFFF);
            std::printf("ok az_playerinfo speedXZ=%.4f rot=(%d,%d) "
                        "playerYaw=%d addr=0x%08x\n",
                        speedXZ, rx, ry, playerYaw, *head);
        }
        else if (cmd == "soh_wallinfo") {
            if (!g_soh_booted) { PrintErr("soh_wallinfo: run soh_boot first"); continue; }
            unsigned int bgFlags = 0; int wallYaw = 0, wallBgId = 0;
            unsigned long wallPoly = 0; float speedXZ = 0, velY = 0;
            if (!SohState_PlayerWallInfo(&bgFlags, &wallYaw, &wallBgId,
                                         &wallPoly, &speedXZ, &velY)) {
                PrintErr("soh_wallinfo: no player"); continue;
            }
            std::printf("ok bgFlags=0x%04x wallYaw=%d wallBgId=%d wallPoly=0x%lx "
                        "speedXZ=%.3f velY=%.3f\n",
                        bgFlags, wallYaw, wallBgId, wallPoly, speedXZ, velY);
        }
        else if (cmd == "soh_tp") {
            std::string xs, ys, zs, yaws;
            if (!(toks >> xs) || !(toks >> ys) || !(toks >> zs)) {
                PrintErr("soh_tp: usage: soh_tp <x> <y> <z> [yaw_s16]"); continue;
            }
            float x = std::stof(xs), y = std::stof(ys), z = std::stof(zs);
            if (!g_soh_booted) { PrintErr("soh_tp: run soh_boot first"); continue; }
            if (!SohState_TeleportPlayer(x, y, z)) {
                PrintErr("soh_tp: no player"); continue;
            }
            int yaw_out = 0; bool yaw_set = false;
            if (toks >> yaws) {
                auto v = ParseNum(yaws);
                if (!v) { PrintErr("soh_tp: bad yaw"); continue; }
                yaw_out = (int)*v;
                if (!SohState_SetPlayerYaw(yaw_out)) {
                    PrintErr("soh_tp: no player (yaw)"); continue;
                }
                yaw_set = true;
            }
            if (yaw_set)
                std::printf("ok soh_tp %.2f %.2f %.2f yaw=%d\n", x, y, z, yaw_out);
            else
                std::printf("ok soh_tp %.2f %.2f %.2f\n", x, y, z);
        }
        else if (cmd == "az_playerpos") {
            // Read Az Player Actor world.pos + world.rot.y + live playerYaw.
            // Used by the sweep to match SoH's start to Az's OoT3D-repacked
            // spawn coord at Kokiri Forest.
            auto ps = CurrentPlayState();
            if (!ps) { PrintErr("az_playerpos: no playstate"); continue; }
            auto& mem = Core::System::GetInstance().Memory();
            auto head = mem.Read32OrNullopt(*ps + ACTORCTX_OFF +
                                            ACTOR_LISTS_OFF + 2 * 8 + 4);
            if (!head || *head == 0) {
                PrintErr("az_playerpos: no Player actor"); continue;
            }
            auto px = mem.Read32OrNullopt(*head + ACTOR_POS_OFF + 0);
            auto py = mem.Read32OrNullopt(*head + ACTOR_POS_OFF + 4);
            auto pz = mem.Read32OrNullopt(*head + ACTOR_POS_OFF + 8);
            auto rot = mem.Read32OrNullopt(*head + ACTOR_ROT_OFF + 0);
            auto yaw_v = mem.Read32OrNullopt(*head + (PLAYER_YAW_OFF & ~3u));
            if (!px || !py || !pz || !rot || !yaw_v) {
                PrintErr("az_playerpos: mem read fail"); continue;
            }
            float x, y, z;
            std::memcpy(&x, &*px, 4);
            std::memcpy(&y, &*py, 4);
            std::memcpy(&z, &*pz, 4);
            short worldRy = static_cast<short>((*rot >> 16) & 0xFFFF);
            short playerYaw = static_cast<short>(
                (*yaw_v >> ((PLAYER_YAW_OFF & 2) * 8)) & 0xFFFF);
            std::printf("ok az_playerpos pos=(%.4f,%.4f,%.4f) "
                        "worldRy=%d playerYaw=%d\n",
                        x, y, z, worldRy, playerYaw);
        }
        else if (cmd == "soh_input") {
            std::string bs, xs, ys;
            if (!(toks >> bs)) { PrintErr("soh_input: usage: soh_input <button-mask> [stickX] [stickY]"); continue; }
            auto b = ParseNum(bs);
            if (!b) { PrintErr("soh_input: bad mask"); continue; }
            int sx = 0, sy = 0;
            if (toks >> xs) { auto v = ParseNum(xs); if (v) sx = (int)*v; }
            if (toks >> ys) { auto v = ParseNum(ys); if (v) sy = (int)*v; }
            if (!g_soh_booted) { PrintErr("soh_input: run soh_boot first"); continue; }
            int ok = SohState_SetInput((unsigned int)(*b & 0xFFFF), sx, sy);
            if (!ok) { PrintErr("soh_input: no playstate — soh_step until Play is up first"); continue; }
            std::printf("ok soh_input 0x%04x stick=(%d,%d)\n",
                        (unsigned)(*b & 0xFFFF), sx, sy);
        }
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
            "  Kill it first: <safekill> soh3d_harness\n"
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
