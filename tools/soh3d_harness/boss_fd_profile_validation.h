#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_PROFILE_VALIDATION_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_PROFILE_VALIDATION_H

#include "boss_fd_oracle.h"
#include "soh_boss_fd_state.h"

namespace HarnessBossFdProfile {

bool MatchesComparisonScope(const HarnessBossFdOracle::State& oracle, const BossFdNativeInputs& native,
                            const BossFdAuthoredState& authored, float tolerance);
bool MatchesForcedInitialization(const HarnessBossFdOracle::State& oracle, const BossFdNativeInputs& native);

} // namespace HarnessBossFdProfile

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_PROFILE_VALIDATION_H
