#include "ship/Context.h"
#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardScancodes.h"
#include <atomic>
#include <cstring>
#include <iostream>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include "ship/install_config.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/scripted/ScriptedInputFifo.h"
#include "ship/debug/CrashHandler.h"
#include "ship/window/FileDropMgr.h"
#include "ship/events/EventSystem.h"
#ifdef ENABLE_SCRIPTING
#include "ship/scripting/ScriptLoader.h"
#include "ship/security/Keystore.h"
#endif

#ifdef _WIN32
#include <libloaderapi.h>
#include <tchar.h>
#include <windows.h>
#include <stringapiset.h>
#endif

#ifdef __APPLE__
#include "ship/utils/AppleFolderManager.h"
#include <unistd.h>
#include <pwd.h>
#endif

namespace Ship {
std::unique_ptr<Context> Context::mContext;

// Every Context::Init* below opens with "if this subsystem already exists, return true". For ONE
// game that is harmless idempotence. For a SECOND game core it is silent wrong state, and the return
// value is the trap: returning true makes Context::Init's aggregate && chain report complete success
// while none of the second game's own subsystems were installed -- it keeps the first game's
// archives, config, audio, console, window and input. InitControlDeck is the sharpest case, since
// each game passes a ControlDeck built with its OWN button list and the second one is dropped.
//
// Behaviour is deliberately unchanged (still true): callers assume success and would fail sooner and
// less clearly otherwise. What changes is that the skip becomes VISIBLE -- but ONLY when it is
// actually dangerous, which is the part that had to be measured rather than assumed.
//
// A blanket warning here was WRONG, and a normal single-game boot proved it: `zelda3d oot` alone
// trips InitConfiguration and InitConsoleVariables, which really are called twice during one game's
// startup. So these guards serve two purposes at once -- genuine idempotence within a game, and the
// cross-game hazard -- and only the second is a problem.
//
// The discriminator is therefore not "was this skipped" but "is a DIFFERENT game attached".
// CreateUninitializedInstance already detects that by app name; it raises the flag below, and only
// then does a skip mean a core inherited another game's subsystem. Fires exactly in the dangerous
// case, silent in the normal one.
//
// The real fix is the per-game/engine split in docs/MM_NATIVE.md N3, where each Init must
// distinguish "already initialised for THIS session" from "initialised for a DIFFERENT game".
static std::atomic<bool> sForeignCoreAttached{ false };

static bool AlreadyInitialised(const char* subsystem) {
    if (!sForeignCoreAttached) {
        return true; // ordinary idempotence within one game
    }
    SPDLOG_ERROR("Context::Init{} skipped -- this core has INHERITED the previous game's {}. "
                 "See docs/MM_NATIVE.md N3.",
                 subsystem, subsystem);
    return true;
}

Context* Context::GetRawInstance() {
    return mContext.get();
}

void Context::DestroyInstance() {
    mContext = nullptr;
}

Context::~Context() {
    SPDLOG_TRACE("destruct context");
    // Stop the scripted-input FIFO poller (no-op if it was never started) before tearing down.
    Ship_ScriptedInputFifo_Stop();
    GetWindow()->SaveWindowToConfig();
    // Explicitly destructing everything so that logging is done last.
    mAudio = nullptr;
    mWindow = nullptr;
    mConsole = nullptr;
    mCrashHandler = nullptr;
    mControlDeck = nullptr;
    mResourceManager = nullptr;
    mConsoleVariables = nullptr;
    mEventSystem = nullptr;
#ifdef ENABLE_SCRIPTING
    if (mScriptLoader) {
        mScriptLoader->UnloadAll();
    }
    mScriptLoader = nullptr;
    mKeystore = nullptr;
#endif
    GetConfig()->Save();
    mConfig = nullptr;
    mLogger->flush();
    mLogger = nullptr;
#ifndef _DEBUG
    mLogThreadPool = nullptr;
#endif
}

Context* Context::CreateInstance(const std::string& name, const std::string& shortName,
                                 const std::string& configFilePath, const std::vector<std::string>& archivePaths,
                                 const std::unordered_set<uint32_t>& validHashes, uint32_t reservedThreadCount,
                                 AudioSettings audioSettings, std::shared_ptr<Window> window,
                                 std::shared_ptr<ControlDeck> controlDeck) {
    if (mContext == nullptr) {
        mContext = std::make_unique<Context>(name, shortName, configFilePath);
        if (mContext->Init(archivePaths, validHashes, reservedThreadCount, audioSettings, window, controlDeck)) {
            return mContext.get();
        } else {
            SPDLOG_ERROR("Failed to initialize");
            return nullptr;
        };
    }

    SPDLOG_DEBUG("Trying to create a context when it already exists. Returning existing.");

    return GetRawInstance();
}

void Context::EarlyLogToStderr() {
    // See declaration comment. Idempotent — calling twice is a no-op.
    if (spdlog::get("ship_pre_init") != nullptr) return;
    try {
        auto _early = spdlog::stderr_color_mt("ship_pre_init");
        spdlog::set_default_logger(_early);
    } catch (const spdlog::spdlog_ex&) {
        // Registry already has it or is locked; nothing to do.
    }
}

// C-linkage wrapper so harnesses (soh3d_harness) can install the early
// stderr sink without pulling in Ship::Context's header + spdlog headers.
extern "C" void Ship_EarlyLogToStderr(void) {
    Context::EarlyLogToStderr();
}

Context* Context::CreateUninitializedInstance(const std::string& name, const std::string& shortName,
                                              const std::string& configFilePath) {
    if (mContext == nullptr) {
        // Belt-and-suspenders: also install here for callers that don't
        // pre-call EarlyLogToStderr(). Idempotent.
        EarlyLogToStderr();
        mContext = std::make_unique<Context>(name, shortName, configFilePath);
        return mContext.get();
    }

    // Reaching here means a SECOND game core called InitOTR while a first core's Context was still
    // alive -- the launcher running cores back to back (`zelda3d --run-sequence`). The caller asked
    // for its own Context and is silently handed someone else's, complete with the OTHER game's
    // ResourceManager, archive set, config file and app name. It does not fail here; it fails much
    // later and somewhere unrelated (measured: OoT after MM dies in CreateFontWithSize).
    //
    // So this is an ERROR, not a debug note. It was SPDLOG_DEBUG, which is invisible in a normal
    // run, and that silence is the whole problem: a wrong-game Context looked exactly like success.
    // Still returns the existing instance rather than nullptr -- every caller assumes non-null and
    // would crash sooner and less clearly -- but the log now names both games, so the mismatch is
    // identifiable at the point it happens instead of at the eventual crash.
    //
    // The real fix is splitting Ship::Context's engine-lifetime state (window, renderer, ImGui)
    // from its per-game state (archives, config, name); see docs/MM_NATIVE.md N3.
    if (mContext->GetName() != name) {
        SPDLOG_ERROR("Context already exists for \"{}\"; \"{}\" is being handed that one instead of its own. "
                     "Its archives, config and app name will be the WRONG GAME's. See docs/MM_NATIVE.md N3.",
                     mContext->GetName(), name);
        // From here every Init* skip means this core inherited the other game's subsystem, so the
        // guards start reporting. Before this point they are ordinary idempotence and stay quiet.
        sForeignCoreAttached = true;
    } else {
        SPDLOG_WARN("Context for \"{}\" already exists; returning it rather than creating a second.", name);
    }

    return GetRawInstance();
}

Context::Context(std::string name, std::string shortName, std::string configFilePath)
    : mConfigFilePath(std::move(configFilePath)), mName(std::move(name)), mShortName(std::move(shortName)) {
}

bool Context::Init(const std::vector<std::string>& archivePaths, const std::unordered_set<uint32_t>& validHashes,
                   uint32_t reservedThreadCount, AudioSettings audioSettings, std::shared_ptr<Window> window,
                   std::shared_ptr<ControlDeck> controlDeck) {
    return InitLogging() && InitConfiguration() && InitConsoleVariables() &&
           InitResourceManager(archivePaths, validHashes, reservedThreadCount) && InitControlDeck(controlDeck) &&
           InitCrashHandler() && InitConsole() && InitWindow(window) && InitAudio(audioSettings) &&
#ifdef ENABLE_SCRIPTING
           InitEventSystem() && InitFileDropMgr() && InitScriptLoader();
#else
           InitEventSystem() && InitFileDropMgr();
#endif
}

bool Context::InitLogging(spdlog::level::level_enum debugBuildLogLevel,
                          spdlog::level::level_enum releaseBuildLogLevel) {
    if (GetLogger() != nullptr) {
        return AlreadyInitialised("Logging");
    }

    try {
        // Setup Logging
        spdlog::init_thread_pool(8192, 1);
        std::vector<spdlog::sink_ptr> sinks;

#if (!defined(_WIN32)) || defined(_DEBUG)
#if defined(_DEBUG) && defined(_WIN32)
        // LLVM on Windows allocs a hidden console in its entrypoint function.
        // We free that console here to create our own.
        FreeConsole();
        if (AllocConsole() == 0) {
            throw std::system_error(GetLastError(), std::generic_category(), "Failed to create debug console");
        }

        SetConsoleOutputCP(CP_UTF8);

        FILE* fDummy;
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
        freopen_s(&fDummy, "CONOUT$", "w", stderr);
        freopen_s(&fDummy, "CONIN$", "r", stdin);
        std::cout.clear();
        std::clog.clear();
        std::cerr.clear();
        std::cin.clear();

        HANDLE hConOut = CreateFile(_T("CONOUT$"), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        HANDLE hConIn = CreateFile(_T("CONIN$"), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        SetStdHandle(STD_OUTPUT_HANDLE, hConOut);
        SetStdHandle(STD_ERROR_HANDLE, hConOut);
        SetStdHandle(STD_INPUT_HANDLE, hConIn);
        std::wcout.clear();
        std::wclog.clear();
        std::wcerr.clear();
        std::wcin.clear();
#endif
        // Log to STDERR, not STDOUT — otherwise driver scripts using stdout
        // as a REPL wire protocol (e.g. tools/soh3d_harness with soh_boot)
        // get log lines interleaved with command responses. Terminals see
        // no difference; pipes get a clean protocol channel.
        auto systemConsoleSink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        systemConsoleSink->set_level(spdlog::level::trace);
        sinks.push_back(systemConsoleSink);
#endif

        auto logPath = GetPathRelativeToAppDirectory(("logs/" + GetName() + ".log"));
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath, 1024 * 1024 * 10, 10);
        sinks.push_back(fileSink);
#ifdef _DEBUG
        mLogger = std::make_shared<spdlog::logger>("multi_sink", sinks.begin(), sinks.end());
        GetLogger()->set_level(debugBuildLogLevel);
        GetLogger()->flush_on(spdlog::level::trace);
#else
        mLogThreadPool = std::make_shared<spdlog::details::thread_pool>(8192, 1);
        mLogger = std::make_shared<spdlog::async_logger>(GetName(), sinks.begin(), sinks.end(), mLogThreadPool,
                                                         spdlog::async_overflow_policy::block);
        GetLogger()->set_level(releaseBuildLogLevel);
        GetLogger()->flush_on(spdlog::level::info);
#endif
        GetLogger()->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%@] [%l] %v");

        spdlog::register_logger(GetLogger());
        spdlog::set_default_logger(GetLogger());
        return true;
    } catch (const spdlog::spdlog_ex& ex) {
        std::cout << "Log initialization failed: " << ex.what() << std::endl;
        return false;
    }
}

bool Context::InitConfiguration() {
    if (GetConfig() != nullptr) {
        return AlreadyInitialised("Configuration");
    }

    mConfig = std::make_shared<Config>(GetPathRelativeToAppDirectory(mConfigFilePath));

    if (GetConfig() == nullptr) {
        SPDLOG_ERROR("Failed to initialize config");
        return false;
    }

    return true;
}

bool Context::InitConsoleVariables() {
    if (GetConsoleVariables() != nullptr) {
        return AlreadyInitialised("ConsoleVariables");
    }

    mConsoleVariables = std::make_shared<ConsoleVariable>();

    if (GetConsoleVariables() == nullptr) {
        SPDLOG_ERROR("Failed to initialize console variables");
        return false;
    }

    return true;
}

bool Context::InitResourceManager(const std::vector<std::string>& archivePaths,
                                  const std::unordered_set<uint32_t>& validHashes, uint32_t reservedThreadCount,
                                  const bool allowEmptyPaths) {
    if (GetResourceManager() != nullptr) {
        return AlreadyInitialised("ResourceManager");
    }

#ifdef ENABLE_SCRIPTING
    InitKeystore();
#endif

    mMainPath = GetConfig()->GetString("Game.Main Archive", GetAppDirectoryPath());
    mPatchesPath = GetConfig()->GetString("Game.Patches Archive", GetAppDirectoryPath() + "/mods");
    if (archivePaths.empty()) {
        std::vector<std::string> paths = std::vector<std::string>();
        paths.push_back(mMainPath);
        paths.push_back(mPatchesPath);

        mResourceManager = std::make_unique<ResourceManager>();
        GetResourceManager()->Init(paths, validHashes, reservedThreadCount);
    } else {
        mResourceManager = std::make_unique<ResourceManager>();
        GetResourceManager()->Init(archivePaths, validHashes, reservedThreadCount);
    }

    if (!allowEmptyPaths && !GetResourceManager()->IsLoaded()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OTR file not found",
                                 "Main OTR file not found. Please generate one", nullptr);
        SPDLOG_ERROR("Main OTR file not found!");
#ifdef __IOS__
        // We need this exit to close the app when we dismiss the dialog
        exit(0);
#endif
        return false;
    }

    return true;
}

bool Context::InitControlDeck(std::shared_ptr<ControlDeck> controlDeck) {
    if (GetControlDeck() != nullptr) {
        return AlreadyInitialised("ControlDeck");
    }

    mControlDeck = controlDeck;

    if (GetControlDeck() == nullptr) {
        SPDLOG_ERROR("Failed to initialize control deck");
        return false;
    }

    // Start the game-agnostic scripted-input FIFO poller once the control deck is up. Hooked here
    // (not in Context::Init) because MM/2s2h boots via the individual Init* methods rather than the
    // aggregate Init(); InitControlDeck is the one input-init both OoT (zelda3d) and MM share. No-op
    // unless SHIP_SCRIPTED_FIFO is set, so live OoT and normal MM runs are untouched.
    Ship_ScriptedInputFifo_StartFromEnv();

    return true;
}

bool Context::InitCrashHandler() {
    if (GetCrashHandler() != nullptr) {
        return AlreadyInitialised("CrashHandler");
    }

    mCrashHandler = std::make_shared<CrashHandler>();

    if (GetCrashHandler() == nullptr) {
        SPDLOG_ERROR("Failed to initialize crash handler");
        return false;
    }

    return true;
}

bool Context::InitAudio(AudioSettings settings) {
    if (GetAudio() != nullptr) {
        return AlreadyInitialised("Audio");
    }

    mAudio = std::make_shared<Audio>(settings);

    if (GetAudio() == nullptr) {
        SPDLOG_ERROR("Failed to initialize audio");
        return false;
    }

    GetAudio()->Init();
    return true;
}

bool Context::InitConsole() {
    if (GetConsole() != nullptr) {
        return AlreadyInitialised("Console");
    }

    mConsole = std::make_shared<Console>();

    if (GetConsole() == nullptr) {
        SPDLOG_ERROR("Failed to initialize console");
        return false;
    }

    GetConsole()->Init();

    return true;
}

bool Context::InitWindow(std::shared_ptr<Window> window) {
    if (GetWindow() != nullptr) {
        return AlreadyInitialised("Window");
    }

    mWindow = window;

    if (GetWindow() == nullptr) {
        SPDLOG_ERROR("Failed to initialize window");
        return false;
    }

    GetWindow()->Init();

    return true;
}

bool Context::InitFileDropMgr() {
    if (GetFileDropMgr() != nullptr) {
        return AlreadyInitialised("FileDropMgr");
    }

    mFileDropMgr = std::make_shared<FileDropMgr>();
    if (GetFileDropMgr() == nullptr) {
        SPDLOG_ERROR("Failed to initialize file drop manager");
        return false;
    }
    return true;
}

bool Context::InitEventSystem() {
    if (GetEventSystem() != nullptr) {
        return AlreadyInitialised("EventSystem");
    }

    mEventSystem = std::make_shared<EventSystem>();
    if (GetEventSystem() == nullptr) {
        SPDLOG_ERROR("Failed to initialize event system");
        return false;
    }
    return true;
}

#ifdef ENABLE_SCRIPTING
bool Context::InitScriptLoader(std::unordered_map<std::string, std::string> compileDefines, int codeVersion,
                               std::string buildOptions, std::vector<std::string> includePaths,
                               std::vector<std::string> libraryPaths, std::vector<std::string> libraries) {
    if (GetScriptLoader() != nullptr) {
        return AlreadyInitialised("ScriptLoader");
    }

    mScriptLoader = std::make_shared<ScriptLoader>(compileDefines, codeVersion, buildOptions, includePaths,
                                                   libraryPaths, libraries);
    if (GetScriptLoader() == nullptr) {
        SPDLOG_ERROR("Failed to initialize script system");
        return false;
    }
    return true;
}

bool Context::InitKeystore() {
    if (GetKeystore() != nullptr) {
        return AlreadyInitialised("Keystore");
    }

    mKeystore = std::make_shared<Keystore>();
    if (GetKeystore() == nullptr) {
        SPDLOG_ERROR("Failed to initialize keystore system");
        return false;
    }
    return true;
}
#endif // ENABLE_SCRIPTING

std::shared_ptr<ConsoleVariable> Context::GetConsoleVariables() const {
    return mConsoleVariables;
}

std::shared_ptr<spdlog::logger> Context::GetLogger() const {
    return mLogger;
}

std::shared_ptr<Config> Context::GetConfig() const {
    return mConfig;
}

std::shared_ptr<ResourceManager> Context::GetResourceManager() const {
    return mResourceManager;
}

std::shared_ptr<ControlDeck> Context::GetControlDeck() const {
    return mControlDeck;
}

std::shared_ptr<CrashHandler> Context::GetCrashHandler() const {
    return mCrashHandler;
}

std::shared_ptr<Window> Context::GetWindow() const {
    return mWindow;
}

std::shared_ptr<Console> Context::GetConsole() const {
    return mConsole;
}

std::shared_ptr<Audio> Context::GetAudio() const {
    return mAudio;
}

std::shared_ptr<FileDropMgr> Context::GetFileDropMgr() const {
    return mFileDropMgr;
}

std::shared_ptr<EventSystem> Context::GetEventSystem() const {
    return mEventSystem;
}

#ifdef ENABLE_SCRIPTING
std::shared_ptr<ScriptLoader> Context::GetScriptLoader() const {
    return mScriptLoader;
}

std::shared_ptr<Keystore> Context::GetKeystore() const {
    return mKeystore;
}
#endif

std::string Context::GetName() const {
    return mName;
}

std::string Context::GetShortName() const {
    return mShortName;
}

// Set by the launcher to the directory of the game core it loaded; empty means "derive from the
// executable", which is what a directly-run soh.elf / mm.elf wants. See SetAppBundlePath.
static std::string sAppBundlePathOverride;

// Written from the game thread, read by the graph loop each frame. See RequestExit.
static std::atomic<bool> sExitRequested{ false };
static std::atomic<bool> sFullTeardownRequested{ false };

void Context::RequestExit() {
    sExitRequested = true;
}

bool Context::IsExitRequested() {
    return sExitRequested;
}

void Context::RequestExitWithFullTeardown() {
    // Order matters: the teardown flag must be visible before the loop can stop, or DeinitOTR could
    // read it while still false and take the _exit path, making the experiment silently measure the
    // ordinary shutdown instead.
    sFullTeardownRequested = true;
    sExitRequested = true;
}

bool Context::IsFullTeardownRequested() {
    return sFullTeardownRequested;
}

void Context::SetAppBundlePath(const std::string& path) {
    sAppBundlePathOverride = path;
}

std::string Context::GetAppBundlePath() {
    // Checked before every platform branch: when a core has been loaded from elsewhere, its own
    // directory is the answer on every platform, and falling through to a platform default would
    // silently reintroduce the executable's directory.
    if (!sAppBundlePathOverride.empty()) {
        return sAppBundlePathOverride;
    }

#if defined(__ANDROID__)
    // SDL3-MIGRATION: SDL_AndroidGetExternalStoragePath -> SDL_GetAndroidExternalStoragePath (Android-only; not built on Linux)
    const char* externaldir = SDL_GetAndroidExternalStoragePath();
    if (externaldir != NULL) {
        return externaldir;
    }
#endif

#ifdef __IOS__
    const char* home = getenv("HOME");
    return std::string(home) + "/Documents";
#endif

#ifdef NON_PORTABLE
    return CMAKE_INSTALL_PREFIX;
#else
#ifdef __APPLE__
    FolderManager folderManager;
    return folderManager.getMainBundlePath();
#endif

#ifdef __linux__
    std::string progpath(PATH_MAX, '\0');
    int len = readlink("/proc/self/exe", &progpath[0], progpath.size() - 1);
    if (len != -1) {
        progpath.resize(len);

        // Find the last '/' and remove everything after it
        long unsigned int lastSlash = progpath.find_last_of("/");
        if (lastSlash != std::string::npos) {
            progpath.erase(lastSlash);
        }

        return progpath;
    }
#endif

#ifdef _WIN32
    std::wstring progpath(MAX_PATH, '\0');

    int len = GetModuleFileNameW(NULL, &progpath[0], progpath.size());
    if (len != 0 && len < progpath.size()) {
        progpath.resize(len);

        // Find the last '\' and remove everything after it
        long unsigned int lastSlash = progpath.find_last_of('\\');
        if (lastSlash != std::string::npos) {
            progpath.erase(lastSlash);
        }

        // Convert wstring to string
        len = WideCharToMultiByte(CP_UTF8, 0, progpath.data(), (int)progpath.size(), nullptr, 0, nullptr, nullptr);
        std::string newProgpath(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, progpath.data(), (int)progpath.size(), &newProgpath[0], len, nullptr, nullptr);

        return newProgpath;
    }
#endif

    return ".";
#endif
}

std::string Context::GetAppDirectoryPath(const std::string& appName) {
#if defined(__ANDROID__)
    // SDL3-MIGRATION: SDL_AndroidGetExternalStoragePath -> SDL_GetAndroidExternalStoragePath (Android-only; not built on Linux)
    const char* externaldir = SDL_GetAndroidExternalStoragePath();
    if (externaldir != NULL) {
        return externaldir;
    }
#endif

#ifdef __IOS__
    const char* home = getenv("HOME");
    return std::string(home) + "/Documents";
#endif

#if defined(__APPLE__)
    FolderManager foldermanager;
    if (char* fpath = std::getenv("SHIP_HOME")) {
        const char* appBundleID = strrchr(fpath, '/');
        if (appBundleID != nullptr) {
            foldermanager.CreateAppSupportDirectory(appBundleID + 1);
        }
        if (fpath[0] == '~') {
            const char* home = getenv("HOME") ? getenv("HOME") : getpwuid(getuid())->pw_dir;
            return std::string(home) + std::string(fpath).substr(1);
        }
        return std::string(fpath);
    }
#endif

#if defined(__linux__)
    char* fpath = std::getenv("SHIP_HOME");
    if (fpath != NULL) {
        return std::string(fpath);
    }
#endif

#ifdef NON_PORTABLE
    const std::string& effectiveAppName = appName.empty() ? GetInstance()->mShortName : appName;
    char* prefpath = SDL_GetPrefPath(NULL, effectiveAppName.c_str());
    if (prefpath != NULL) {
        std::string ret(prefpath);
        SDL_free(prefpath);
        return ret;
    }
#endif

    return ".";
}

std::string Context::GetPathRelativeToAppBundle(const std::string& path) {
    return GetAppBundlePath() + "/" + path;
}

std::string Context::GetPathRelativeToAppDirectory(const std::string& path, const std::string& appName) {
    return GetAppDirectoryPath(appName) + "/" + path;
}

std::string Context::LocateFileAcrossAppDirs(const std::string& path, const std::string& appName) {
    std::string fpath;

    // app configuration dir
    fpath = GetPathRelativeToAppDirectory(path, appName);
    if (std::filesystem::exists(fpath)) {
        return fpath;
    }
    // app install dir
    fpath = GetPathRelativeToAppBundle(path);
    if (std::filesystem::exists(fpath)) {
        return fpath;
    }
    // current dir
    return "./" + std::string(path);
}

} // namespace Ship
