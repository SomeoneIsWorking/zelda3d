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
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "libretro.h"

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
        // HW context needed. Every other core variable falls through to the
        // core's built-in default.
        auto* var = static_cast<retro_variable*>(data);
        if (var->key && std::strcmp(var->key, "citra_graphics_api") == 0) {
            var->value = "Software";
            return true;
        }
        var->value = nullptr;
        return false;
    }

    default:
        return false;
    }
}

void VideoRefresh(const void*, unsigned, unsigned, size_t) {}
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
    for (uint64_t i = 0; i < *n; ++i) retro_run();
    std::printf("ok run %llu\n", static_cast<unsigned long long>(*n));
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
bool g_soh_booted = false;

void HandleSohBoot(std::istringstream&) {
    if (g_soh_booted) { PrintErr("soh_boot: already booted"); return; }
    // Force headless — this is a Linux dev machine with Wayland; SoH3D's
    // libultraship must NOT try to open a window.
    setenv("SOH3D_HEADLESS", "1", 1);
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
    for (uint64_t i = 0; i < *n; ++i) RunFrame();
    std::printf("ok soh_step %llu\n", static_cast<unsigned long long>(*n));
}

void HandleDiag(std::istringstream&) {
    std::printf("ok mask=0x%08x polls=%llu ids_seen=0x%08x\n",
                g_input_mask,
                static_cast<unsigned long long>(g_input_poll_count),
                g_input_poll_ids_seen);
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

void HandlePlayState(std::istringstream&) {
    auto ps = CurrentPlayState();
    if (!ps) { PrintErr("playstate: not populated (still in menu/title?)"); return; }
    std::printf("ok 0x%08x\n", *ps);
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
        "  quit                 exit\n"
        "  help                 this list\n");
    std::printf("ok\n");
}

void RunRepl() {
    std::string line;
    while (std::getline(std::cin, line)) {
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
        else if (cmd == "input")     HandleInput(toks);
        else if (cmd == "diag")      HandleDiag(toks);
        else if (cmd == "loadstate") HandleLoadState(toks);
        else if (cmd == "savestate") HandleSaveState(toks);
        else if (cmd == "playstate") HandlePlayState(toks);
        else if (cmd == "scene")     HandleScene(toks);
        else if (cmd == "warp")      HandleWarp(toks);
        else if (cmd == "actors")    HandleActors(toks);
        else if (cmd == "soh_boot")  HandleSohBoot(toks);
        else if (cmd == "soh_step")  HandleSohStep(toks);
        else if (cmd == "help")      PrintHelp();
        else if (cmd == "quit") { std::printf("ok\n"); std::fflush(stdout); return; }
        else                         PrintErr(("unknown cmd: " + cmd).c_str());
        std::fflush(stdout);
    }
}

} // namespace

int main(int argc, char** argv) {
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

    retro_init();

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

    if (!retro_load_game(&game)) {
        std::fprintf(stderr, "soh3d_harness: retro_load_game returned false\n");
        retro_deinit();
        return EXIT_FAILURE;
    }

    std::printf("boot succeeded\n");
    std::fflush(stdout);

    RunRepl();

    if (g_soh_booted) {
        Main_Shutdown();
        DeinitOTR();
        Heaps_Free();
    }
    retro_unload_game();
    retro_deinit();
    return EXIT_SUCCESS;
}
