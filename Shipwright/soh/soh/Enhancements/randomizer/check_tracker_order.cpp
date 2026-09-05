#include "check_tracker_order.h"
#include "SeedContext.h"

#include "item_location.h"
#include "location.h"
#include "soh/OTRGlobals.h"

namespace CheckTracker {
namespace {

bool IsEoDCheck(RandomizerCheckType type) {
    return type == RCTYPE_BOSS_HEART_OR_OTHER_REWARD || type == RCTYPE_DUNGEON_REWARD;
}

} // namespace

bool CompareChecks(RandomizerCheck i, RandomizerCheck j) {
    Rando::Location* x = Rando::StaticData::GetLocation(i);
    Rando::Location* y = Rando::StaticData::GetLocation(j);
    auto itemI = OTRGlobals::Instance->gRandoContext->GetItemLocation(i);
    auto itemJ = OTRGlobals::Instance->gRandoContext->GetItemLocation(j);
    bool iCollected = itemI->HasObtained();
    bool iSaved = itemI->GetCheckStatus() == RCSHOW_SAVED;
    bool jCollected = itemJ->HasObtained();
    bool jSaved = itemJ->GetCheckStatus() == RCSHOW_SAVED;

    if (!iCollected && jCollected) {
        return true;
    } else if (iCollected && !jCollected) {
        return false;
    }

    if (!iSaved && jSaved) {
        return true;
    } else if (iSaved && !jSaved) {
        return false;
    }

    if (!itemI->GetIsSkipped() && itemJ->GetIsSkipped()) {
        return true;
    } else if (itemI->GetIsSkipped() && !itemJ->GetIsSkipped()) {
        return false;
    }

    if (!IsEoDCheck(x->GetRCType()) && IsEoDCheck(y->GetRCType())) {
        return true;
    } else if (IsEoDCheck(x->GetRCType()) && !IsEoDCheck(y->GetRCType())) {
        return false;
    }

    if (i < j) {
        return true;
    } else if (i > j) {
        return false;
    }

    return false;
}

} // namespace CheckTracker
