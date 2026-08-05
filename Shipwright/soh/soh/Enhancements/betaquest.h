#pragma once

// Beta Quest enable state. See betaquest.cpp for why this is not menu code.
//
// z_play.c declares enableBetaQuest/disableBetaQuest itself (z_play.c:93-94) rather than including
// this header, which is how the pair survived a "no callers" grep. The declarations are kept here so
// C++ callers have a real header, and so the next person greps their way to the definition.

#ifdef __cplusplus
extern "C" {
#endif

void enableBetaQuest(void);
void disableBetaQuest(void);

#ifdef __cplusplus
}

extern bool isBetaQuestEnabled;
#endif
