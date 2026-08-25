#include "randomizer_generation_policy.h"

#include "3drando/menu.hpp"
#include "SeedContext.h"
#include "soh/cvar_prefixes.h"
#include "randomizer_check_objects.h"
#include "settings.h"
#include "static_data.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <set>
#include <sstream>

namespace {
std::set<RandomizerCheck> ReadExcludedLocations() {
    std::set<RandomizerCheck> excludedLocations;
    std::stringstream values(CVarGetString(CVAR_RANDOMIZER_SETTING("ExcludedLocations"), ""));
    std::string value;
    while (getline(values, value, ',')) {
        excludedLocations.insert(static_cast<RandomizerCheck>(std::stoi(value)));
    }
    return excludedLocations;
}

std::set<RandomizerTrick> ReadEnabledTricks() {
    std::set<RandomizerTrick> enabledTricks;
    std::stringstream values(CVarGetString(CVAR_RANDOMIZER_SETTING("EnabledTricks"), ""));
    std::string value;
    while (getline(values, value, ',')) {
        if (Rando::StaticData::trickToEnum.contains(value)) {
            enabledTricks.insert(Rando::StaticData::trickToEnum[value]);
        }
    }
    return enabledTricks;
}

void RemoveHiddenExcludedLocations(std::set<RandomizerCheck>& excludedLocations) {
    RandomizerCheckObjects::UpdateImGuiVisibility();

    const auto context = Rando::Context::GetInstance();
    for (const auto& location : Rando::StaticData::GetLocationTable()) {
        const auto check = location.GetRandomizerCheck();
        const auto excluded = excludedLocations.find(check);
        if (!context->GetItemLocation(check)->IsVisible() && excluded != excludedLocations.end()) {
            excludedLocations.erase(excluded);
        }
    }
}
} // namespace

bool GenerateRandomizerWithCurrentSettings(const std::string& seed) {
    // RANDOTODO proper UI for selecting if a spoiler loaded should be used for settings.
    Rando::Settings::GetInstance()->SetAllToContext();

    auto excludedLocations = ReadExcludedLocations();
    auto enabledTricks = ReadEnabledTricks();
    RemoveHiddenExcludedLocations(excludedLocations);
    return GenerateRandomizer(excludedLocations, enabledTricks, seed);
}
