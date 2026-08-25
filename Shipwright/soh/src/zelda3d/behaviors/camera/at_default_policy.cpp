#include "at_default_policy.h"

namespace Zelda3D {

bool CameraAtDefaultUsesExtraYBranch(int floorType, bool isGetItemAction) {
    // FUN_00250AD0 gives the stock slope branch the three special floor types only when the
    // current action is not Player_Action_8084E6D4. Keep that recovered predicate in one owner.
    const bool isSlopeFloor = floorType == 4 || floorType == 7 || floorType == 12;
    return !isSlopeFloor || isGetItemAction;
}

} // namespace Zelda3D
