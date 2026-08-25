#include "first_div_gameplay_compare.h"

#include "first_div_actor_compare.h"
#include "first_div_gameplay_camera_compare.h"
#include "first_div_player_compare.h"
#include "first_div_reporter.h"

#include <cstdio>

namespace HarnessOracle {

void CompareGameplayFirstDiv(std::uint32_t oraclePlayState, FirstDivReporter& reporter) {
    const GameplayPlayerComparison playerComparison = CompareFirstDivPlayer(oraclePlayState, reporter);
    CompareGameplayCameraFirstDiv(oraclePlayState, playerComparison, reporter);
    CompareFirstDivActors(oraclePlayState, reporter);

    if (!reporter.Reported()) {
        std::printf("  firstdiv: none — all 7 play-mode dimensions matched\n");
    }
}

} // namespace HarnessOracle
