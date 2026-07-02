// soh3d_harness — headless libretro-frontend host that drives Azahar's
// libretro core in-process (no dlopen, no .so). Azahar's citra_libretro
// source files are linked directly into this executable (see the sibling
// CMakeLists.txt), so the retro_* entry points are just normal C symbols.
//
// This is the first scaffold toward the direction laid out in
// soh3d/CLAUDE.md ("Direction: build a direct harness that EMBEDS Azahar as
// a library, not runs it").
//
// Current milestone: retro_load_game succeeds, retro_run pumps frames
// through the software renderer, and the harness reads OoT3D VA directly
// via Azahar's Memory::MemorySystem — proven by observing gPlayState
// (0x0050AF34) transition from 0 → an allocated pointer once the game
// populates it. Scripted warp writes land next.
//
// Usage:
//   soh3d_harness [rom_path] [--frames N]
//   soh3d_harness                        # rom = $ZELDA3D_OOT3D_ROM, N=300
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

namespace {

std::string g_system_dir;
std::string g_save_dir;

bool Read32(uint32_t va, uint32_t& out) {
    auto v = Core::System::GetInstance().Memory().Read32OrNullopt(va);
    if (!v) return false;
    out = *v;
    return true;
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

void VideoRefresh(const void* /*data*/, unsigned /*width*/, unsigned /*height*/, size_t /*pitch*/) {}
void AudioSample(int16_t /*l*/, int16_t /*r*/) {}
size_t AudioSampleBatch(const int16_t* /*data*/, size_t frames) { return frames; }
void InputPoll() {}
int16_t InputState(unsigned /*port*/, unsigned /*device*/, unsigned /*index*/, unsigned /*id*/) {
    return 0;
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

} // namespace

int main(int argc, char** argv) {
    std::string rom_path;
    int frames = 300;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (a.rfind("--", 0) == 0) {
            std::fprintf(stderr, "soh3d_harness: unknown option: %s\n", a.c_str());
            return EXIT_FAILURE;
        } else if (rom_path.empty()) {
            rom_path = a;
        }
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

    std::fprintf(stdout, "boot succeeded\n");

    // Frame-pump loop. Each retro_run() call runs the emulator until it
    // submits one video frame — same "one frame per run" cadence RetroArch
    // uses. We poll gPlayState (0x0050AF34, a pointer to PlayState — see
    // tools/oracle_motion_sample.py and oot3d-decomp docs/actor_layout.md)
    // and log the moment the game populates it, plus the u16 sceneNum at
    // PlayState+0x104. Pure observation for now; warp writes land next.
    constexpr uint32_t GPLAYSTATE_VA = 0x0050AF34;
    constexpr uint32_t SCENENUM_OFF  = 0x104;
    uint32_t last_ps = 0;
    uint16_t last_scene = 0xFFFF;
    for (int f = 0; f < frames; ++f) {
        retro_run();
        uint32_t ps = 0;
        if (Read32(GPLAYSTATE_VA, ps) && ps != last_ps) {
            uint32_t raw = 0;
            uint16_t scene = 0xFFFF;
            if (ps != 0 && Read32(ps + SCENENUM_OFF, raw)) {
                scene = static_cast<uint16_t>(raw & 0xFFFF);
            }
            std::fprintf(stderr,
                         "soh3d_harness: frame=%d gPlayState=0x%08x sceneNum=0x%04x\n",
                         f, ps, scene);
            last_ps = ps;
            last_scene = scene;
        }
    }
    std::fprintf(stderr, "soh3d_harness: ran %d frames, final gPlayState=0x%08x sceneNum=0x%04x\n",
                 frames, last_ps, last_scene);

    retro_unload_game();
    retro_deinit();
    return EXIT_SUCCESS;
}
