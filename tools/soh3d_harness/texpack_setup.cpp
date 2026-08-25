#include "texpack_setup.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "asset/texpack.h"
#include "common/settings.h"
#include "core/core.h"
#include "libretro_frontend.h"
#include "video_core/custom_textures/custom_tex_manager.h"

namespace HarnessFrontend {

namespace {

bool g_texpack_on = false;
std::string g_texpack_root;

// Both renderers must consume the same texels. A valid pack root follows the
// shipping Zelda3D convention and contains at least one tex1_*.png.
bool TexPackRootValid(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    std::error_code error;
    if (!fs::is_directory(root, error)) {
        return false;
    }
    for (auto iterator = fs::recursive_directory_iterator(root, error);
         !error && iterator != fs::recursive_directory_iterator(); iterator.increment(error)) {
        if (iterator->is_regular_file(error) && iterator->path().filename().string().rfind("tex1_", 0) == 0) {
            return true;
        }
    }
    return false;
}

std::string ResolveTexPackRoot(const std::string& romPath) {
    namespace fs = std::filesystem;
    std::error_code error;
    auto absolute = [&](const fs::path& path) { return fs::absolute(path, error).lexically_normal().string(); };
    if (const char* environment = std::getenv("ZELDA3D_TEXPACK"); environment && *environment) {
        if (!std::strcmp(environment, "0") || !std::strcmp(environment, "off") || !std::strcmp(environment, "none")) {
            return {};
        }
        if (TexPackRootValid(environment)) {
            return absolute(environment);
        }
        std::fprintf(stderr, "harness: texpack: ZELDA3D_TEXPACK=%s has no tex1_*.png\n", environment);
        return {};
    }
    if (TexPackRootValid("textures")) {
        return absolute("textures");
    }
    if (!romPath.empty()) {
        const fs::path candidate = fs::path(romPath).parent_path() / "textures";
        if (TexPackRootValid(candidate)) {
            return absolute(candidate);
        }
    }
    return {};
}

} // namespace

void SetupTexPack(const std::string& romPath) {
    namespace fs = std::filesystem;
    const char* mode = std::getenv("ZELDA3D_HARNESS_TEXPACK");
    const bool wanted = !(mode && (!std::strcmp(mode, "0") || !std::strcmp(mode, "off") || !std::strcmp(mode, "none")));
    g_texpack_root = wanted ? ResolveTexPackRoot(romPath) : std::string{};
    g_texpack_on = !g_texpack_root.empty();

    if (!g_texpack_on) {
        Settings::values.custom_textures = false;
        std::fprintf(stderr, "harness: texpack: OFF on BOTH sides (%s)\n",
                     wanted ? "no pack found" : "ZELDA3D_HARNESS_TEXPACK=off");
        return;
    }

    // Azahar's LoadDir points at the same root Zelda3D indexes. No pack is
    // copied, so one directory remains authoritative for the comparison.
    std::error_code error;
    const fs::path userDirectory = fs::absolute(SaveDirectory(), error) / "Azahar";
    const fs::path loadDirectory = userDirectory / "load";
    const fs::path link = loadDirectory / "textures";
    fs::create_directories(loadDirectory, error);
    if (fs::is_symlink(link, error)) {
        if (fs::read_symlink(link, error) != fs::path(g_texpack_root)) {
            fs::remove(link, error);
        }
    } else if (fs::exists(link, error)) {
        std::fprintf(stderr, "harness: texpack: %s exists and is NOT a symlink — leaving it\n", link.c_str());
    }
    if (!fs::exists(link, error)) {
        fs::create_directory_symlink(g_texpack_root, link, error);
    }
    if (error) {
        std::fprintf(stderr,
                     "harness: texpack: could not link %s -> %s (%s); oracle stays "
                     "VANILLA, disabling our side too to keep the A/B honest\n",
                     link.c_str(), g_texpack_root.c_str(), error.message().c_str());
        g_texpack_on = false;
        g_texpack_root.clear();
        Settings::values.custom_textures = false;
        return;
    }

    // Azahar's synchronous custom-texture path re-enters its cached-memory
    // bookkeeping and aborts. Keep its default asynchronous loader; callers
    // must step until hit counters stop growing before an evidence capture.
    Settings::values.custom_textures = true;
    std::fprintf(stderr, "harness: texpack: ON on BOTH sides — root %s (az load dir %s)\n", g_texpack_root.c_str(),
                 loadDirectory.c_str());
}

void HandleTexPack(std::istringstream& arguments) {
    const auto zeldaStats = Zelda3D::TexPackGetStats();
    const auto oracleStats = Core::System::GetInstance().CustomTexManager().GetStats();
    std::printf("ok texpack mode=%s root=%s az=%zu/%zu az_hits=%llu/%llu "
                "soh=%llu soh_hits=%llu/%llu%s\n",
                TexPackEnabled() ? "on" : "off", TexPackEnabled() ? TexPackRoot().c_str() : "-", oracleStats.files,
                oracleStats.materials, static_cast<unsigned long long>(oracleStats.hits),
                static_cast<unsigned long long>(oracleStats.misses),
                static_cast<unsigned long long>(zeldaStats.indexed), static_cast<unsigned long long>(zeldaStats.hits),
                static_cast<unsigned long long>(zeldaStats.misses),
                zeldaStats.scanned ? "" : " (soh side not scanned yet)");
    (void)arguments;
}

bool TexPackEnabled() {
    return g_texpack_on;
}

const std::string& TexPackRoot() {
    return g_texpack_root;
}

} // namespace HarnessFrontend
