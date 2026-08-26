#include "soh_runtime.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <unistd.h>

#include "frame_watchdog.h"
#include "frontend_presentation.h"
#include "libretro_frontend.h"
#include "repl_protocol.h"
#include "texpack_setup.h"

extern "C" {
void GameConsole_Init(void);
void InitOTR(int argc, char* argv[]);
void CrashHandler_PrintSohData(char*, size_t*);
typedef void (*CrashHandlerCallback)(char*, size_t*);
void CrashHandlerRegisterCallback(CrashHandlerCallback callback);
void BootCommands_Init(void);
void Heaps_Alloc(void);
void Zelda3D_CoreRunBegin(void);
void Zelda3D_GL_SetProgressCallback(void (*callback)(void));
void Main_Init(void* arg);
void RunFrame(void);
}

namespace HarnessSohRuntime {
namespace {

using FrameWatchdog = HarnessWatchdog::Frame;
using HarnessRepl::ParseNum;
using HarnessRepl::PrintErr;

bool gBooted = false;

void LinkRuntimeAsset(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::error_code error;
    if (!std::filesystem::exists(source, error) ||
        std::filesystem::exists(std::filesystem::symlink_status(destination, error))) {
        return;
    }
    std::filesystem::create_directories(destination.parent_path(), error);
    std::filesystem::create_symlink(source, destination, error);
    if (error) {
        std::fprintf(stderr, "harness: WARNING could not link %s -> %s (%s)\n", destination.c_str(), source.c_str(),
                     error.message().c_str());
    }
}

void PrepareWorkingDirectory() {
    char cwd[1024];
    if (getcwd(cwd, sizeof cwd) == nullptr) {
        return;
    }

    const std::filesystem::path repoRoot = cwd;
    std::filesystem::path sohCwd = repoRoot / "scratch/harness/soh_cwd";
    if (const char* overridePath = std::getenv("ZELDA3D_HARNESS_SOH_CWD")) {
        sohCwd = overridePath;
    }
    std::error_code error;
    std::filesystem::create_directories(sohCwd, error);

    // SoH resolves archives from cwd and extractor assets next to the executable.
    std::filesystem::path shippingBuild = repoRoot / "Shipwright/build-cmake";
    if (const char* configuredBuild = std::getenv("ZELDA3D_SHIPPING_BUILD_DIR")) {
        shippingBuild = configuredBuild;
    }
    const std::filesystem::path built = shippingBuild / "soh";
    LinkRuntimeAsset(built / "oot.o2r", sohCwd / "oot.o2r");
    LinkRuntimeAsset(built / "soh.o2r", sohCwd / "soh.o2r");

    char executable[1024];
    const ssize_t length = readlink("/proc/self/exe", executable, sizeof executable - 1);
    if (length > 0) {
        executable[length] = '\0';
        LinkRuntimeAsset(built / "assets", std::filesystem::path(executable).parent_path() / "assets");
    }

    if (chdir(sohCwd.c_str()) == 0) {
        std::fprintf(stderr, "harness: SoH cwd isolated -> %s\n", sohCwd.c_str());
    } else {
        std::fprintf(stderr,
                     "harness: WARNING could not chdir to %s — the embedded SoH "
                     "will read/WRITE shipofharkinian.json in the launch cwd\n",
                     sohCwd.c_str());
    }
}

void ConfigureEnvironment() {
    setenv("SOH_HEADLESS", "1", 1);
    setenv("SOH3D_HEADLESS", "1", 1);
    setenv("ZELDA3D_LAUNCHER", "0", 1);
    setenv("ZELDA3D_TEXPACK", HarnessFrontend::TexPackEnabled() ? HarnessFrontend::TexPackRoot().c_str() : "off", 1);
}

void WriteHarnessConfiguration() {
    int width = 400 * HarnessFrontend::ResolutionFactor();
    int height = 240 * HarnessFrontend::ResolutionFactor();
    if (const char* configuredWidth = std::getenv("ZELDA3D_HARNESS_SOH_W")) {
        const int value = std::atoi(configuredWidth);
        if (value > 0) {
            width = value;
        }
    }
    if (const char* configuredHeight = std::getenv("ZELDA3D_HARNESS_SOH_H")) {
        const int value = std::atoi(configuredHeight);
        if (value > 0) {
            height = value;
        }
    }

    std::FILE* file = std::fopen("shipofharkinian.json", "w");
    if (file == nullptr) {
        return;
    }
    std::fprintf(file,
                 "{\n"
                 "  \"Window\": { \"Width\": %d, \"Height\": %d },\n"
                 "  \"CVars\": { \"gInternalResolution\": 1.0 }\n"
                 "}\n",
                 width, height);
    std::fclose(file);
}

} // namespace

bool IsBooted() {
    return gBooted;
}

void Boot() {
    PrepareWorkingDirectory();
    ConfigureEnvironment();
    WriteHarnessConfiguration();

    HarnessFrontend::EnsureWindow();
    static char executableName[] = "soh3d_harness";
    static char* arguments[] = { executableName, nullptr };

    // Establish the run epoch before any once-per-run subsystem initialization.
    Zelda3D_CoreRunBegin();
    Zelda3D_GL_SetProgressCallback(HarnessWatchdog::Pulse);
    GameConsole_Init();
    InitOTR(1, arguments);
    CrashHandlerRegisterCallback(&CrashHandler_PrintSohData);
    BootCommands_Init();
    Heaps_Alloc();
    Main_Init(nullptr);
    gBooted = true;
}

void AdvanceFrame(const char* watchdogContext) {
    HarnessFrontend::RequestSohCapture(gBooted);
    FrameWatchdog watchdog(watchdogContext);
    RunFrame();
}

void HandleBoot(std::istringstream&) {
    if (gBooted) {
        PrintErr("soh_boot: already booted");
        return;
    }
    Boot();
    std::printf("ok soh_boot\n");
}

void HandleStep(std::istringstream& arguments) {
    if (!gBooted) {
        PrintErr("soh_step: run soh_boot first");
        return;
    }

    std::string countText;
    if (!(arguments >> countText)) {
        PrintErr("soh_step: usage: soh_step <N>");
        return;
    }
    const auto count = ParseNum(countText);
    if (!count) {
        PrintErr("soh_step: bad N");
        return;
    }

    uint64_t completed = 0;
    for (uint64_t frame = 0; frame < *count; ++frame) {
        if (HarnessFrontend::QuitRequested()) {
            break;
        }
        AdvanceFrame("HandleSohStep/RunFrame");
        ++completed;
    }
    std::printf("ok soh_step %llu\n", static_cast<unsigned long long>(completed));
}

} // namespace HarnessSohRuntime
