#pragma once

#include "randomizerTypes.h"

namespace CheckTracker {

// Uncollected, unsaved, unskipped checks precede completed checks; dungeon rewards come last.
bool CompareChecks(RandomizerCheck lhs, RandomizerCheck rhs);

} // namespace CheckTracker
