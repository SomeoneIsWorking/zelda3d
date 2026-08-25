#include "randomizer_generation_bridge.h"

#include "SeedContext.h"
#include "soh/cvar_prefixes.h"
#include "randomizer_generation_lifecycle.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <cstdio>
#include <string>

namespace {
struct GenerationRequest {
    std::string seed;
    bool wait;
};

GenerationRequest ParseGenerationRequest(const char* request) {
    GenerationRequest result{ request != nullptr ? request : "", true };
    if (result.seed.rfind("async", 0) == 0) {
        result.wait = false;
        result.seed = result.seed.substr(5);
        while (!result.seed.empty() && result.seed.front() == ' ') {
            result.seed.erase(result.seed.begin());
        }
    }
    return result;
}

const char* DisplaySeed(const std::string& seed) {
    return seed.empty() ? "(random)" : seed.c_str();
}
} // namespace

// Headless entry point for seed generation. The optional "async" prefix is the only path that
// leaves generation in flight so shutdown's join behavior can be exercised without a frame loop.
extern "C" void Zelda3D_RandoGenerateBlocking(const char* seed, char* out, int outSize) {
    const auto request = ParseGenerationRequest(seed);
    if (!GenerateRandomizer(request.seed)) {
        std::snprintf(out, outSize, "randogen: DECLINED -- a generation is already running (RandoGenerating=%d)",
                      CVarGetInteger(CVAR_GENERAL("RandoGenerating"), 0));
        return;
    }
    if (!request.wait) {
        std::snprintf(out, outSize,
                      "randogen: ASYNC started, seed='%s' -- NOT waited for; this run covers the "
                      "in-flight case, not a completed seed",
                      DisplaySeed(request.seed));
        return;
    }

    JoinRandoGenerationThread();
    const auto context = Rando::Context::GetInstance();
    std::snprintf(out, outSize, "randogen: seed='%s' generated=%d hash='%s' seedString='%s'", DisplaySeed(request.seed),
                  context->IsSeedGenerated() ? 1 : 0, context->GetHash().c_str(), context->GetSeedString().c_str());
}
