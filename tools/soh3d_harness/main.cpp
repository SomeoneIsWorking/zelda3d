// soh3d_harness — headless libretro-frontend host that drives Azahar's
// libretro core in-process (no dlopen, no .so). Azahar's citra_libretro
// source files are linked directly into this executable (see the sibling
// CMakeLists.txt), so the retro_* entry points are just normal C symbols.
//
// This is the first scaffold toward the direction laid out in
// soh3d/CLAUDE.md ("Direction: build a direct harness that EMBEDS Azahar as
// a library, not runs it").
//
// Milestone for this commit: prove Azahar's libretro core accepts and
// validates the OoT3D ROM (retro_load_game returns true) with a null
// video/audio/input path. Not booting to a scene yet — the software renderer
// / HW context plumbing lands next.
//
// Usage:
//   soh3d_harness [rom_path]
//   soh3d_harness                  # rom = $ZELDA3D_OOT3D_ROM
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "libretro.h"

namespace {

std::string g_system_dir;
std::string g_save_dir;

retro_memory_map g_memory_map{};
std::vector<retro_memory_descriptor> g_memory_descs;

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

    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
        const auto* mm = static_cast<const retro_memory_map*>(data);
        g_memory_descs.assign(mm->descriptors, mm->descriptors + mm->num_descriptors);
        g_memory_map.descriptors = g_memory_descs.data();
        g_memory_map.num_descriptors = static_cast<unsigned>(g_memory_descs.size());
        std::fprintf(stderr, "soh3d_harness: captured %u memory descriptor(s)\n",
                     g_memory_map.num_descriptors);
        return true;
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

    std::fprintf(stdout, "boot succeeded\n");

    retro_unload_game();
    retro_deinit();
    return EXIT_SUCCESS;
}
