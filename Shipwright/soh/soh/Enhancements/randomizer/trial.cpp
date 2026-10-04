#include "trial.h"
#include "static_data.h"

#include <libultraship/log/luslog.h>

namespace Rando {
// std::move removed: both parameters are enums, which are trivially copyable, so it had no effect and
// clang-tidy rejects it (performance-move-const-arg). Behaviour is identical.
TrialInfo::TrialInfo(RandomizerHintTextKey nameKey_, TrialKey trialKey_) : nameKey(nameKey_), trialKey(trialKey_) {
}
TrialInfo::TrialInfo() = default;
TrialInfo::~TrialInfo() = default;

CustomMessage TrialInfo::GetName() const {
    return StaticData::hintTextTable[nameKey].GetHintMessage();
}

RandomizerHintTextKey TrialInfo::GetNameKey() const {
    return nameKey;
}

TrialKey TrialInfo::GetTrialKey() const {
    return trialKey;
}

bool TrialInfo::IsSkipped() const {
    return skipped;
}

bool TrialInfo::IsRequired() const {
    return !skipped;
}

void TrialInfo::SetAsRequired() {
    skipped = false;
}

void TrialInfo::SetAsSkipped() {
    skipped = true;
}

Trials::Trials() {
    for (const auto trial : StaticData::trialData) {
        mTrials[trial.first] = TrialInfo(trial.second, static_cast<TrialKey>(trial.first));
    }
}
Trials::~Trials() = default;

namespace {
// Where GetTrial sends a TrialKey that is not one of the real trials. Audited 2026-08-13,
// docs/issues/0023.
//
// It cannot be nullptr: SaveManager::LoadRandomizer dereferences the result unconditionally
// (`randoContext->GetTrial(trialId)->SetAsRequired()`) and that call is exactly the one carrying a
// save-JSON-controlled `trialId`, so nullptr would trade the silent corruption this guard exists to
// stop for a null-pointer write in the middle of a save load.
//
// Built with the two-argument constructor on purpose. TrialInfo() is = default and leaves nameKey
// UNINITIALIZED, and GetName() would index hintTextTable with it -- so a default-constructed sink
// would still be a crash, just a different one. RHT_NONE keeps GetName() in range and TK_MAX states
// plainly that this is not one of the trials, which nothing indexes by: GetTrialList,
// GetAllTrialHintHeys and ParseJson all walk mTrials, never this object.
TrialInfo sRefusedTrial{ RHT_NONE, TK_MAX };
} // namespace

TrialInfo* Trials::GetTrial(const TrialKey key) {
    // mTrials has no spare row and GetTrial does not carry its own length, so an unchecked key is a
    // TrialInfo-shaped write into the following member. trialId reaches this accessor straight from
    // the save JSON array "requiredTrials" (SaveManager::LoadRandomizer), which makes it
    // user-controlled on this port. See sRefusedTrial above for why a refused key lands there
    // instead of in a neighbour trial.
    const size_t index = static_cast<size_t>(key);
    if (index >= mTrials.size()) {
        LUSLOG_ERROR("Trials::GetTrial: trial id %zu is out of range (valid 0..%zu) -- REFUSED, "
                     "no trial was modified",
                     index, mTrials.size() - 1);
        return &sRefusedTrial;
    }
    return &mTrials[index];
}

void Trials::SkipAll() {
    for (TrialInfo& trial : mTrials) {
        trial.SetAsSkipped();
    }
}

void Trials::RequireAll() {
    for (TrialInfo& trial : mTrials) {
        trial.SetAsRequired();
    }
}

std::vector<TrialInfo*> Trials::GetTrialList() {
    std::vector<TrialInfo*> trialList{};
    for (size_t i = 0; i < mTrials.size(); i++) {
        trialList.push_back(&mTrials[i]);
    }
    return trialList;
}

size_t Trials::GetTrialListSize() const {
    return mTrials.size();
}

void Trials::ParseJson(nlohmann::json spoilerFileJson) {
    nlohmann::json trialsJson = spoilerFileJson["requiredTrials"];

    for (auto& trial : mTrials) {
        trial.SetAsSkipped();

        for (auto nameInLang : trial.GetName().GetAllMessages()) {
            if (std::find(trialsJson.begin(), trialsJson.end(), nameInLang) != trialsJson.end()) {
                trial.SetAsRequired();
            }
        }
    }
}

std::unordered_map<uint32_t, RandomizerHintTextKey> Trials::GetAllTrialHintHeys() const {
    std::unordered_map<uint32_t, RandomizerHintTextKey> output = {};
    for (auto trial : mTrials) {
        output[(uint32_t)trial.GetTrialKey()] = trial.GetNameKey();
    }
    return output;
}

} // namespace Rando
