#include "betaquest.h"

// Beta Quest's enable flag, and the two setters the game engine calls.
//
// These lived in SohGui/SohMenuEnhancements.cpp, buried among ~1,800 lines of Dear ImGui menu
// building. They are not menu code: z_play.c calls them directly (Play_Init disables, the beta-quest
// warp path enables), so they run whether or not any menu exists. They moved out so that file could
// be deleted with the rest of the dead ImGui tree.
//
// A grep-based survey reported this pair as having "zero callers" — it does not, and the C call
// sites are why: `extern "C"` declarations in z_play.c, not an #include of any SohGui header, so
// nothing tied the definition to its users that a header search could see.

bool isBetaQuestEnabled = false;

extern "C" {

void enableBetaQuest() {
    isBetaQuestEnabled = true;
}

void disableBetaQuest() {
    isBetaQuestEnabled = false;
}
}
