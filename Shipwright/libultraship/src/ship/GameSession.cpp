#include "ship/GameSession.h"

#include <spdlog/spdlog.h>

#include "ship/config/Config.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/resource/ResourceManager.h"

namespace Ship {

GameSession::GameSession(std::string name, std::string shortName, std::string configFilePath)
    : mName(std::move(name)), mShortName(std::move(shortName)), mConfigFilePath(std::move(configFilePath)) {
}

GameSession::~GameSession() {
    End();
}

void GameSession::End() {
    // The order mirrors Context's destructor, which is where it was established: input first, then
    // the archives, then the CVars -- each of which may write into Config on the way down -- and
    // Config saved and released last. Getting this backwards loses settings silently, so it is
    // copied deliberately rather than re-derived.
    mControlDeck = nullptr;
    mResourceManager = nullptr;
    mConsoleVariables = nullptr;
    if (mConfig != nullptr) {
        mConfig->Save();
        mConfig = nullptr;
    }
}

void GameSession::NoteInstalled(const std::string& subsystem) {
    mInstalled.insert(subsystem);
}

bool GameSession::WasInstalledByThisSession(const std::string& subsystem) const {
    return mInstalled.count(subsystem) != 0;
}

const std::string& GameSession::GetName() const {
    return mName;
}

const std::string& GameSession::GetShortName() const {
    return mShortName;
}

const std::string& GameSession::GetConfigFilePath() const {
    return mConfigFilePath;
}

} // namespace Ship
