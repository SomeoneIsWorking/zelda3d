#include "libretro_frontend.h"

#include <atomic>
#include <filesystem>

namespace HarnessFrontend {

namespace {

std::string g_system_dir;
std::string g_save_dir;
std::atomic<bool> g_quit_requested{ false };

} // namespace

void ConfigureDirectories() {
    std::error_code error;
    namespace fs = std::filesystem;
    fs::create_directories("scratch/harness/system", error);
    fs::create_directories("scratch/harness/save", error);
    g_system_dir = fs::absolute("scratch/harness/system", error).lexically_normal().string();
    g_save_dir = fs::absolute("scratch/harness/save", error).lexically_normal().string();
}

const std::string& SystemDirectory() {
    return g_system_dir;
}

const std::string& SaveDirectory() {
    return g_save_dir;
}

bool QuitRequested() {
    return g_quit_requested.load();
}

void RequestQuit() {
    g_quit_requested.store(true);
}

} // namespace HarnessFrontend
