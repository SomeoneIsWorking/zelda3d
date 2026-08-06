#pragma once

#include <memory>
#include <set>
#include <string>

namespace Ship {

class Config;
class Console;
class ConsoleVariable;
class ControlDeck;
class ResourceManager;

/**
 * @brief The state that belongs to ONE game, separated out of the engine-lifetime Ship::Context.
 *
 * Ship::Context conflated two different lifetimes, and that conflation is what blocked running two
 * games in one process (docs/MM_NATIVE.md N3). Measured, not assumed: `zelda3d --run-sequence mm,oot`
 * showed OoT inheriting MM's ResourceManager, Configuration, ConsoleVariables and ControlDeck, because
 * every Context::Init* opens with "if it already exists, return true" and there was only ever one set
 * of members to test. The engine rows it also inherited -- Window, Console -- are correct to share;
 * that is the entire point of one libultraship.so.
 *
 * So the members here are exactly the per-game rows of that classification:
 *   - the ResourceManager and its archive set  (the definitive one: OoT reading MM's .o2r is the bug)
 *   - the Config object and its file path      (shipofharkinian.json vs 2ship2harkinian.json)
 *   - the ConsoleVariables, which persist into that per-game config and so travel with it
 *   - the ControlDeck, because each game constructs its own with a game-specific button list
 *     (OTRGlobals.cpp / BenPort.cpp) -- the physical-device layer beneath it stays engine-shared
 *   - the archive + mods paths, and the app name/short name the whole lot is keyed by
 *
 * Context owns exactly one of these at a time and every Context accessor forwards to it, so no call
 * site changes: GetResourceManager() still answers "the running game's", it just now has a defined
 * answer to "which game". Attaching a different game ends the previous session and begins a new one.
 *
 * NOT yet split, and deliberately named rather than left to be discovered: Audio and Console are both
 * SPLIT rows in that same classification -- the audio device and the console object are engine, but
 * the sequence player and the registered commands are per-game. They stay whole in Context for now,
 * so a second game still inherits them.
 */
class GameSession {
  public:
    GameSession(std::string name, std::string shortName, std::string configFilePath);
    ~GameSession();

    /**
     * @brief Tear down this game's subsystems in the order Context's destructor established.
     *
     * Config is saved and released LAST because the subsystems above it write into it on the way
     * down; ControlDeck goes first for the same reason the engine teardown puts input early.
     * Idempotent -- the destructor calls it, and so does an explicit end.
     */
    void End();

    /**
     * @brief Record that a per-game subsystem was installed BY THIS SESSION, and ask afterwards.
     *
     * This exists so the "did a core inherit the previous game's state" check tests a fact instead of
     * a hand-written label. A guard that simply sees a non-null member cannot tell the two apart:
     * within one game's startup `InitConfiguration` and `InitConsoleVariables` are genuinely called
     * twice, and the second call is ordinary idempotence -- while the SAME non-null member for a
     * second core would be the bug. The first version of this check could not distinguish them and
     * duly reported two false INHERITED errors on a run where the split had worked correctly.
     *
     * With the installing session recorded, the two are separable: skipped AND this session installed
     * it is idempotence; skipped AND it did NOT is inheritance.
     */
    void NoteInstalled(const std::string& subsystem);
    bool WasInstalledByThisSession(const std::string& subsystem) const;

    const std::string& GetName() const;
    const std::string& GetShortName() const;
    const std::string& GetConfigFilePath() const;

    std::shared_ptr<Config> mConfig;
    std::shared_ptr<Console> mConsole;
    std::shared_ptr<ConsoleVariable> mConsoleVariables;
    std::shared_ptr<ResourceManager> mResourceManager;
    std::shared_ptr<ControlDeck> mControlDeck;

    std::string mMainPath;
    std::string mPatchesPath;

  private:
    std::string mName;
    std::string mShortName;
    std::string mConfigFilePath;
    std::set<std::string> mInstalled;
};

} // namespace Ship
