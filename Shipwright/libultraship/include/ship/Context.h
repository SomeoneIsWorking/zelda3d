#pragma once

#include <string>
#include <memory>
#include <unordered_set>
#include <vector>
#include <unordered_map>
#include <spdlog/async.h>
#include "ship/audio/Audio.h"
#include "ship/GameSession.h"

namespace spdlog {
class logger;
}

namespace Ship {

class Console;
class ConsoleVariable;
class ControlDeck;
class CrashHandler;
class Window;
class Config;
class ResourceManager;
class FileDropMgr;
class EventSystem;
#ifdef ENABLE_SCRIPTING
class ScriptLoader;
class Keystore;
#endif

/**
 * @brief Central singleton context for the libultraship engine.
 *
 * Context owns and provides access to every major subsystem (resource management,
 * window, audio, controller, logging, configuration, scripting, etc.). Exactly one
 * Context should be alive at a time; use GetInstance() to retrieve it after creation.
 *
 * Typical usage:
 * @code
 * auto ctx = Ship::Context::CreateInstance("MyApp", "app", "config.json", archivePaths);
 * // ... use ctx->GetResourceManager(), ctx->GetWindow(), etc.
 * @endcode
 * @note Pointers to @code Context@endcode should not be stored as members. You should always access
 * @code Context@endcode via GetRawInstance
 */
class Context {
  public:
    /**
     * @brief Returns the currently active global Context instance.
     * @return Raw pointer to the Context. This should not be considered shared and should be considered invalid
     * once the scope ends.
     */
    static Context* GetRawInstance();
    static void DestroyInstance();
    /**
     * @brief Install a stderr-backed spdlog default_logger IMMEDIATELY, before
     * any Context is created. Callable from process-start driver code (e.g.
     * soh3d_harness main()) so that pre-InitLogging boot messages don't leak
     * onto stdout (which drivers use as a REPL wire channel).
     */
    static void EarlyLogToStderr();
    /**
     * @brief Creates, initializes, and stores the global Context instance.
     *
     * Convenience factory that calls CreateUninitializedInstance() followed by Init().
     *
     * @param name              Human-readable application name.
     * @param shortName         Short application identifier used for paths and config keys.
     * @param configFilePath    Path to the JSON configuration file.
     * @param archivePaths      List of archive file or directory paths to mount.
     * @param validHashes       Set of acceptable game-version hash values (empty = all allowed).
     * @param reservedThreadCount Number of threads to keep free from the resource thread-pool.
     * @param audioSettings     Initial audio backend and channel configuration.
     * @param window            Optional pre-constructed Window to use; if nullptr a default is created.
     * @param controlDeck       Optional pre-constructed ControlDeck; if nullptr a default is created.
     * @return Raw pointer to the fully initialized Context.
     * @note The pointer returned by this function is only for convenience directly after creating an instance.
     */
    static Context* CreateInstance(const std::string& name, const std::string& shortName,
                                   const std::string& configFilePath, const std::vector<std::string>& archivePaths = {},
                                   const std::unordered_set<uint32_t>& validHashes = {},
                                   uint32_t reservedThreadCount = 1, AudioSettings audioSettings = {},
                                   std::shared_ptr<Window> window = nullptr,
                                   std::shared_ptr<ControlDeck> controlDeck = nullptr);

    /**
     * @brief Creates a Context that has not yet been initialized.
     *
     * Use this when you need finer control over initialization order; call Init() manually afterwards.
     *
     * @param name           Human-readable application name.
     * @param shortName      Short application identifier.
     * @param configFilePath Path to the JSON configuration file.
     * @return Raw pointer to the uninitialized Context.
     * @note The pointer returned by this function is only for convenience directly after creating an instance.
     */
    static Context* CreateUninitializedInstance(const std::string& name, const std::string& shortName,
                                                const std::string& configFilePath);

    /**
     * @brief Returns the platform-specific application bundle directory (e.g. the .app bundle on macOS).
     * @return Absolute path string, or an empty string on platforms without the concept of a bundle.
     */
    static std::string GetAppBundlePath();

    /**
     * @brief Ask the running game to end its frame loop and return, instead of killing the process.
     *
     * Both games' graph loops are `while (WindowIsRunning()) RunFrame();`, so this is honoured at
     * the one seam they share, and what follows is exactly the ordinary window-close path:
     * Main_Shutdown stops the audio thread, then DeinitOTR persists window layout and config.
     *
     * This IS a handoff: the core unwinds all the way out and its run() returns to the launcher,
     * which then either loads the game named by RequestGameSwitch or ends the process. What it does
     * NOT do is tear the engine down -- window and renderer belong to the launcher and stay up for
     * its lifetime (see RequestExitWithFullTeardown for the measurement that settled that).
     */
    static void RequestExit();

    /**
     * @brief Clear the exit request. Called by the launcher before each core's run().
     *
     * The request belongs to a RUN. It used to be cleared in BeginGameSession, which only fires
     * when a different game attaches -- so returning to the chooser (oot -> oot, same session) left
     * the previous run's `quit` latched and the next run unwound on its first frame while every
     * observable reported success.
     */
    static void BeginRun();

    /**
     * @brief Has RequestExit been called? Consulted by WindowIsRunning.
     */
    static bool IsExitRequested();

    /**
     * @brief Exit, and additionally run the REAL engine teardown instead of DeinitOTR's _exit(0).
     *
     * This exists to answer one question, and it is the falsifier recorded on claim C057. DeinitOTR
     * skips the GUI/renderer/window destructors because they crashed in driver code -- RADV/Wayland
     * `wsi_wl_swapchain_destroy` double-free, lavapipe/X11 `xcb_present` overflow, RmlUi's static
     * StyleSheetFactory. Every one of those is from the VULKAN era, and this project has since moved
     * to SDL3 GPU as its only backend, so whether they still reproduce is unknown rather than known.
     *
     * It matters because in-process game switching needs exactly that teardown. If this path
     * survives, the blocker is gone and the `_exit(0)` can go with it; if it crashes, C057's
     * rationale holds on current drivers and the answer is to stop tearing down at all -- give the
     * launcher the window and renderer for the process lifetime and leave cores only their archives
     * and heaps.
     *
     * Reaching the launcher's "core returned" line is the POSITIVE result. A crash is the negative,
     * and both are informative; what would tell us nothing is never running it.
     */
    static void RequestExitWithFullTeardown();

    /**
     * @brief Should DeinitOTR run the real teardown rather than _exit? See RequestExitWithFullTeardown.
     */
    static bool IsFullTeardownRequested();

    /**
     * @brief Ask the host launcher to run a different game once this core returns.
     *
     * The chooser lives inside a game core, and the core it wants to start is one the core cannot
     * see -- RTLD_LOCAL is the whole design. So the request travels through libultraship, the one
     * library both the core and the launcher link, as a game id ("oot", "mm") matching
     * Zelda3DCore::id. This does NOT start anything by itself; the caller must still end its own
     * game (RequestExit), and the launcher acts on the request only after run() has returned.
     *
     * @param gameId  stable game id, or empty to cancel a pending request.
     */
    static void RequestGameSwitch(const std::string& gameId);

    /**
     * @brief Take the pending game-switch request, clearing it.
     *
     * Read-and-clear rather than a plain getter: a request that survived being acted on would make
     * the launcher loop forever on the same game. Returns an empty string when none is pending.
     */
    static std::string TakeRequestedGameSwitch();

    /**
     * @brief Point the bundle path at the running game's own directory, overriding the executable's.
     *
     * This path answers "where does the running game's data live" -- fonts, the RmlUi assets, the
     * extractor's assets/ folder. Deriving it from the EXECUTABLE is correct only while the game IS
     * the executable. Under the launcher (zelda3d_app) the executable is a shell that dlopens a core
     * from another directory, and the unset default resolves to the launcher's own directory, where
     * none of that data exists.
     *
     * The launcher sets this from dladdr() on the loaded core, so the value describes where the game
     * code actually came from rather than being assembled from guesses. Unset means "use the
     * executable's directory", which is exactly right for soh.elf and mm.elf run directly.
     *
     * @param path Absolute directory of the running game, or empty to fall back to the executable's.
     */
    static void SetAppBundlePath(const std::string& path);

    /**
     * @brief Returns the platform-specific directory where the application stores its data.
     * @param appName Override the application name used to build the path; defaults to the current app name.
     * @return Absolute path string.
     */
    static std::string GetAppDirectoryPath(const std::string& appName = "");

    /**
     * @brief Resolves a path relative to the application data directory.
     * @param path    Relative path to resolve.
     * @param appName Override the application name used to build the base path.
     * @return Absolute path string.
     */
    static std::string GetPathRelativeToAppDirectory(const std::string& path, const std::string& appName = "");

    /**
     * @brief Resolves a path relative to the application bundle directory.
     * @param path Relative path to resolve.
     * @return Absolute path string.
     */
    static std::string GetPathRelativeToAppBundle(const std::string& path);

    /**
     * @brief Searches common application directories for a file and returns its absolute path.
     * @param path    Filename or relative path to locate.
     * @param appName Override the application name used to search.
     * @return Absolute path to the first match found, or an empty string if not found.
     */
    static std::string LocateFileAcrossAppDirs(const std::string& path, const std::string& appName = "");

    /**
     * @brief Constructs a Context with the given identifiers but does not initialize subsystems.
     * @param name           Human-readable application name.
     * @param shortName      Short application identifier.
     * @param configFilePath Path to the JSON configuration file.
     */
    Context(std::string name, std::string shortName, std::string configFilePath);
    ~Context();

    /**
     * @brief Initializes all subsystems in the correct order.
     *
     * Called automatically by CreateInstance(). When using CreateUninitializedInstance(),
     * call this method manually after any custom pre-initialization setup.
     *
     * @param archivePaths        List of archive paths to mount.
     * @param validHashes         Acceptable game-version hashes.
     * @param reservedThreadCount Threads to reserve outside the resource pool.
     * @param audioSettings       Audio configuration.
     * @param window              Optional Window override.
     * @param controlDeck         Optional ControlDeck override.
     * @return true on success, false if any subsystem failed to initialize.
     */
    bool Init(const std::vector<std::string>& archivePaths, const std::unordered_set<uint32_t>& validHashes,
              uint32_t reservedThreadCount, AudioSettings audioSettings, std::shared_ptr<Window> window = nullptr,
              std::shared_ptr<ControlDeck> controlDeck = nullptr);

    /** @brief Returns the application-wide spdlog logger. */
    std::shared_ptr<spdlog::logger> GetLogger() const;
    /** @brief Returns the Config subsystem. */
    std::shared_ptr<Config> GetConfig() const;
    /** @brief Returns the ConsoleVariable subsystem (CVars). */
    std::shared_ptr<ConsoleVariable> GetConsoleVariables() const;
    /** @brief Returns the ResourceManager subsystem. */
    std::shared_ptr<ResourceManager> GetResourceManager() const;
    /** @brief Returns the ControlDeck subsystem. */
    std::shared_ptr<ControlDeck> GetControlDeck() const;
    /** @brief Returns the CrashHandler subsystem. */
    std::shared_ptr<CrashHandler> GetCrashHandler() const;
    /** @brief Returns the Window subsystem. */
    std::shared_ptr<Window> GetWindow() const;
    /** @brief Returns the developer Console subsystem. */
    std::shared_ptr<Console> GetConsole() const;
    /** @brief Returns the Audio subsystem. */
    std::shared_ptr<Audio> GetAudio() const;
    /** @brief Returns the FileDropMgr subsystem for handling drag-and-drop file events. */
    std::shared_ptr<FileDropMgr> GetFileDropMgr() const;
    /** @brief Returns the EventSystem subsystem. */
    std::shared_ptr<EventSystem> GetEventSystem() const;
#ifdef ENABLE_SCRIPTING
    /** @brief Returns the ScriptLoader subsystem. */
    std::shared_ptr<ScriptLoader> GetScriptLoader() const;
    /** @brief Returns the Keystore subsystem used for archive signature verification. */
    std::shared_ptr<Keystore> GetKeystore() const;
#endif

    /**
     * @brief End the current game's session and begin a fresh one, keeping the engine alive.
     *
     * This is the seam that lets a second game core initialise in a process where a first has already
     * run. Before it existed, `CreateUninitializedInstance` handed the second core the first's whole
     * Context and every per-game `Init*` then skipped -- returning true -- so the second game ran on
     * the first's archives, config, CVars and button set (measured with `zelda3d --run-sequence
     * mm,oot`; docs/MM_NATIVE.md N3).
     *
     * What it deliberately does NOT touch is the engine half: window, GPU device, renderer, crash
     * handler, logger. Those are shared across games by design -- that is what one libultraship.so is
     * for -- and destroying the window is exactly the teardown that crashes in driver code (claim
     * C057). So this reuses rather than rebuilds them.
     *
     * @param name           Human-readable application name of the incoming game.
     * @param shortName      Short application identifier of the incoming game.
     * @param configFilePath Path to the incoming game's JSON configuration file.
     */
    void BeginGameSession(const std::string& name, const std::string& shortName, const std::string& configFilePath);

    /** @brief The state belonging to the game currently attached. Never null. */
    GameSession* GetGameSession() const;

    /** @brief Returns the human-readable application name. */
    std::string GetName() const;
    /** @brief Returns the short application identifier. */
    std::string GetShortName() const;

    /**
     * @brief Initializes the spdlog logging backend.
     * @param debugBuildLogLevel   Log level used for debug builds.
     * @param releaseBuildLogLevel Log level used for release builds.
     * @return true on success.
     */
    bool InitLogging(spdlog::level::level_enum debugBuildLogLevel = spdlog::level::debug,
                     spdlog::level::level_enum releaseBuildLogLevel = spdlog::level::warn);

    /** @brief Initializes the Config subsystem, loading the config file from disk. */
    bool InitConfiguration();

    /** @brief Initializes the ConsoleVariable (CVar) subsystem and loads persisted values. */
    bool InitConsoleVariables();

    /**
     * @brief Initializes the ResourceManager and mounts the given archives.
     * @param archivePaths      Paths to archives or directories to mount.
     * @param validHashes       Acceptable game-version hashes; empty means all are valid.
     * @param reservedThreadCount Number of threads reserved outside the resource pool.
     * @param allowEmptyPaths   If true, initialization succeeds even when archivePaths is empty.
     * @return true on success.
     */
    bool InitResourceManager(const std::vector<std::string>& archivePaths = {},
                             const std::unordered_set<uint32_t>& validHashes = {}, uint32_t reservedThreadCount = 1,
                             const bool allowEmptyPaths = false);

    /**
     * @brief Initializes the ControlDeck subsystem.
     * @param controlDeck Optional pre-constructed ControlDeck; if nullptr a default is created.
     * @return true on success.
     */
    bool InitControlDeck(std::shared_ptr<ControlDeck> controlDeck = nullptr);

    /** @brief Initializes the CrashHandler subsystem. @return true on success. */
    bool InitCrashHandler();

    /**
     * @brief Initializes the Audio subsystem with the given settings.
     * @param settings Audio backend and channel configuration.
     * @return true on success.
     */
    bool InitAudio(AudioSettings settings);

    /** @brief Initializes the developer Console window. @return true on success. */
    bool InitConsole();

    /**
     * @brief Initializes the Window subsystem.
     * @param window Optional pre-constructed Window; if nullptr a default backend is selected.
     * @return true on success.
     */
    bool InitWindow(std::shared_ptr<Window> window = nullptr);

    /** @brief Initializes the FileDropMgr subsystem. @return true on success. */
    bool InitFileDropMgr();

    /** @brief Initializes the EventSystem subsystem. @return true on success. */
    bool InitEventSystem();

#ifdef ENABLE_SCRIPTING
    /**
     * @brief Initializes the ScriptLoader subsystem for runtime script compilation.
     * @param compileDefines  Preprocessor defines passed to the compiler.
     * @param codeVersion     Version tag embedded in compiled modules.
     * @param buildOptions    Raw compiler flags string.
     * @param includePaths    Additional include directories.
     * @param libraryPaths    Additional library search directories.
     * @param libraries       Libraries to link against compiled scripts.
     * @return true on success.
     */
    bool InitScriptLoader(std::unordered_map<std::string, std::string> compileDefines = {}, int codeVersion = 1,
                          std::string buildOptions = "-g -Wl", std::vector<std::string> includePaths = {},
                          std::vector<std::string> libraryPaths = {}, std::vector<std::string> libraries = {});

    /** @brief Initializes the Keystore used for verifying signed archives. @return true on success. */
    bool InitKeystore();
#endif

  protected:
    Context() = default;

  private:
    static std::unique_ptr<Context> mContext;

    std::shared_ptr<spdlog::logger> mLogger;
    // We only need the spdlog threadpool on release builds because of the async logger.
#ifndef _DEBUG
    std::shared_ptr<spdlog::details::thread_pool> mLogThreadPool;
#endif
    // The per-game half. Config, CVars, the archive set, the button set and the app name all belong
    // to ONE game and are replaced wholesale when a different core attaches -- see GameSession.h and
    // BeginGameSession. Everything below this line is engine-lifetime and is shared across games on
    // purpose. Never null while a Context exists: the constructor opens the first session.
    std::unique_ptr<GameSession> mSession;

    std::shared_ptr<CrashHandler> mCrashHandler;
    std::shared_ptr<Window> mWindow;
    std::shared_ptr<Audio> mAudio;
    std::shared_ptr<FileDropMgr> mFileDropMgr;
    std::shared_ptr<EventSystem> mEventSystem;
#ifdef ENABLE_SCRIPTING
    std::shared_ptr<ScriptLoader> mScriptLoader;
    std::shared_ptr<Keystore> mKeystore;
#endif

};
} // namespace Ship
