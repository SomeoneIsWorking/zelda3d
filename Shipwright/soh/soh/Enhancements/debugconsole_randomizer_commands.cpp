#include "debugconsole_internal.h"

#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "soh/Enhancements/cosmetics/CosmeticsEditor.h"
#include "soh/Enhancements/audio/AudioEditor.h"
#include "soh/Enhancements/randomizer/logic.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/randomizer/randomizer_generation_lifecycle.h"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions/actors.h"
#include "functions/game_state.h"
#include "macros.h"
extern PlayState* gPlayState;
}

static bool GenerateRandoHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                                 std::string* output) {
    try {
        std::string seed;
        if (args.size() > 1) {
            uint32_t value = std::stoi(args[1], nullptr, 10);
            if (args.size() == 3) {
                seed = "seed_testing_count";
            }
            seed += std::to_string(value);
        }

        if (GenerateRandomizer(seed)) {
            return 0;
        }
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] seed|count value must be a number.");
        return 1;
    }

    ERROR_MESSAGE("[SOH] Rando generation already in progress");
    return 1;
}

static constexpr std::array<std::pair<const char*, CosmeticGroup>, COSMETICS_GROUP_MAX> cosmetic_groups = { {
    { "link", COSMETICS_GROUP_LINK },
    { "mirror_shield", COSMETICS_GROUP_MIRRORSHIELD },
    { "swords", COSMETICS_GROUP_SWORDS },
    { "gloves", COSMETICS_GROUP_GLOVES },
    { "equipment", COSMETICS_GROUP_EQUIPMENT },
    { "keyring", COSMETICS_GROUP_KEYRING },
    { "small_keys", COSMETICS_GROUP_SMALL_KEYS },
    { "boss_keys", COSMETICS_GROUP_BOSS_KEYS },
    { "consumable", COSMETICS_GROUP_CONSUMABLE },
    { "hud", COSMETICS_GROUP_HUD },
    { "kaleido", COSMETICS_GROUP_KALEIDO },
    { "title", COSMETICS_GROUP_TITLE },
    { "npc", COSMETICS_GROUP_NPC },
    { "world", COSMETICS_GROUP_WORLD },
    { "magic", COSMETICS_GROUP_MAGIC },
    { "arrows", COSMETICS_GROUP_ARROWS },
    { "spin_attack", COSMETICS_GROUP_SPIN_ATTACK },
    { "trials", COSMETICS_GROUP_TRAILS },
    { "navi", COSMETICS_GROUP_NAVI },
    { "ivan", COSMETICS_GROUP_IVAN },
    { "message", COSMETICS_GROUP_MESSAGE },
} };

static bool CosmeticsHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                             std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }

    if (args[1].compare("reset") == 0) {
        if (args.size() == 2) {
            CosmeticsEditor_ResetAll();
        } else {
            for (const auto& [key, value] : cosmetic_groups) {
                if (args[2].compare(key) == 0) {
                    CosmeticsEditor_ResetGroup(value);
                    return 0;
                }
            }
            ERROR_MESSAGE("[SOH] Invalid argument passed, unrecognized group name");
            return 1;
        }
    } else if (args[1].compare("randomize") == 0) {
        if (args.size() == 2) {
            CosmeticsEditor_RandomizeAll();
        } else {
            for (const auto& [key, value] : cosmetic_groups) {
                if (args[2].compare(key) == 0) {
                    CosmeticsEditor_RandomizeGroup(value);
                    return 0;
                }
            }
            ERROR_MESSAGE("[SOH] Invalid argument passed, unrecognized group name");
            return 1;
        }
    } else {
        ERROR_MESSAGE("[SOH] Invalid argument passed, must be 'reset' or 'randomize'");
        return 1;
    }

    return 0;
}

static std::map<std::string, SeqType> sfx_groups = {
    { "bgm", SEQ_BGM_WORLD },     { "fanfares", SEQ_FANFARE }, { "events", SEQ_BGM_EVENT },
    { "battle", SEQ_BGM_BATTLE }, { "ocarina", SEQ_OCARINA },  { "instruments", SEQ_INSTRUMENT },
    { "sfx", SEQ_SFX },           { "voices", SEQ_VOICE },     { "custom", SEQ_BGM_CUSTOM },
};

static bool SfxHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                       std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }

    if (args[1].compare("reset") == 0) {
        if (args.size() == 2) {
            AudioEditor_ResetAll();
        } else {
            for (const auto& [key, value] : sfx_groups) {
                if (args[2].compare(key) == 0) {
                    AudioEditor_ResetGroup(value);
                    return 0;
                }
            }
            ERROR_MESSAGE("[SOH] Invalid argument passed, unrecognized group name");
            return 1;
        }
    } else if (args[1].compare("randomize") == 0) {
        if (args.size() == 2) {
            AudioEditor_RandomizeAll();
        } else {
            for (const auto& [key, value] : sfx_groups) {
                if (args[2].compare(key) == 0) {
                    AudioEditor_RandomizeGroup(value);
                    return 0;
                }
            }
            ERROR_MESSAGE("[SOH] Invalid argument passed, unrecognized group name");
            return 1;
        }
    } else {
        ERROR_MESSAGE("[SOH] Invalid argument passed, must be 'reset' or 'randomize'");
        return 1;
    }

    return 0;
}

static bool AvailableChecksProcessUndiscoveredExitsHandler(std::shared_ptr<Ship::Console> Console,
                                                           const std::vector<std::string>& args, std::string* output) {
    const auto& logic = Rando::Context::GetInstance()->GetLogic();
    bool enabled = false;

    if (args.size() == 1) {
        enabled = !logic->ACProcessUndiscoveredExits;
    } else {
        try {
            enabled = std::stoi(args[1]);
        } catch ([[maybe_unused]] std::invalid_argument const& ex) {
            ERROR_MESSAGE("[SOH] Enable should be 0 or 1");
            return 1;
        }
    }

    logic->ACProcessUndiscoveredExits = enabled;
    INFO_MESSAGE("[SOH] Available Checks - Process Undiscovered Exits %s",
                 logic->ACProcessUndiscoveredExits ? "enabled" : "disabled");

    CheckTracker::RecalculateAvailableChecks();
    return 0;
}

static bool AvailableChecksRecalculateHandler(std::shared_ptr<Ship::Console> Console,
                                              const std::vector<std::string>& args, std::string* output) {
    RandomizerRegion startingRegion = RR_ROOT;
    RandoAgeTime startingAgeTime = RAT_NONE;

    if (args.size() > 1) {
        try {
            startingRegion = static_cast<RandomizerRegion>(std::stoi(args[1]));
        } catch ([[maybe_unused]] std::invalid_argument const& ex) {
            ERROR_MESSAGE("[SOH] Region should be a number");
            return 1;
        }

        if (startingRegion <= RR_NONE || startingRegion >= RR_MAX) {
            ERROR_MESSAGE("[SOH] Region should be between 1 and %d", RR_MAX - 1);
            return 1;
        }
    }

    if (args.size() > 2) {
        if (args[2] == "ChildDay") {
            startingAgeTime = RAT_CHILD_DAY;
        } else if (args[2] == "ChildNight") {
            startingAgeTime = RAT_CHILD_NIGHT;
        } else if (args[2] == "AdultDay") {
            startingAgeTime = RAT_ADULT_DAY;
        } else if (args[2] == "AdultNight") {
            startingAgeTime = RAT_ADULT_NIGHT;
        } else {
            ERROR_MESSAGE("[SOH] Age Time should be ChildDay, ChildNight, AdultDay, or AdultNight");
        }
    }

    CheckTracker::RecalculateAvailableChecks(startingRegion, startingAgeTime);
    return 0;
}

void DebugConsole_RegisterRandomizerCommands() {
    CMD_REGISTER("gen_rando", { GenerateRandoHandler,
                                "Generate a randomizer seed",
                                {
                                    { "seed|count", Ship::ArgumentType::NUMBER, true },
                                    { "testing", Ship::ArgumentType::NUMBER, true },
                                } });

    CMD_REGISTER("cosmetics", { CosmeticsHandler,
                                "Change cosmetics.",
                                {
                                    { "reset|randomize", Ship::ArgumentType::TEXT },
                                    { "group name", Ship::ArgumentType::TEXT, true },
                                } });

    CMD_REGISTER("sfx", { SfxHandler,
                          "Change SFX.",
                          {
                              { "reset|randomize", Ship::ArgumentType::TEXT },
                              { "group_name", Ship::ArgumentType::TEXT, true },
                          } });

    CMD_REGISTER("acpue", { AvailableChecksProcessUndiscoveredExitsHandler,
                            "Available Checks - Process Undiscovered Exits",
                            { { "enable", Ship::ArgumentType::NUMBER, true } } });

    Ship::Context::GetRawInstance()->GetConsole()->AddCommand(
        "acr", { AvailableChecksRecalculateHandler,
                 "Available Checks - Recalculate",
                 {
                     { "starting_region", Ship::ArgumentType::NUMBER, true },
                     { "ChildDay|ChildNight|AdultDay|AdultNight", Ship::ArgumentType::TEXT, true },
                 } });
}
