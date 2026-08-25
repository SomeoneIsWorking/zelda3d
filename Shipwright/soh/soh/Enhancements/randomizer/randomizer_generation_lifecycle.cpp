#include "randomizer_generation_lifecycle.h"

#include "SeedContext.h"
#include "soh/cvar_prefixes.h"
#include "randomizer_generation_policy.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>

#include <spdlog/spdlog.h>

#include <thread>

namespace {
std::thread randoThread;

void GenerateRandomizerFromCurrentSettings(std::string seed) {
    CVarSetInteger(CVAR_GENERAL("RandoGenerating"), 1);
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();

    Rando::Context::GetInstance()->SetSeedGenerated(GenerateRandomizerWithCurrentSettings(seed));
    CVarSetInteger(CVAR_GENERAL("RandoGenerating"), 0);
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();

    GameInteractor::Instance->ExecuteHooks<GameInteractor::OnGenerationCompletion>();
}
} // namespace

bool GenerateRandomizer(std::string seed) {
    JoinRandoGenerationThread();
    if (CVarGetInteger(CVAR_GENERAL("RandoGenerating"), 0) == 0) {
        randoThread = std::thread(&GenerateRandomizerFromCurrentSettings, seed);
        return true;
    }
    return false;
}

// A joinable thread at process teardown causes std::terminate. DeinitOTR calls this alongside the
// other thread stops, including when generation is still in flight.
void JoinRandoGenerationThread() {
    if (!randoThread.joinable()) {
        SPDLOG_INFO("JoinRandoGenerationThread: no generation thread to join.");
        return;
    }

    const bool inFlight = CVarGetInteger(CVAR_GENERAL("RandoGenerating"), 0) != 0;
    SPDLOG_INFO("JoinRandoGenerationThread: joining the generation thread ({}).",
                inFlight ? "IN FLIGHT -- waiting for it to finish" : "already finished");
    randoThread.join();
}
