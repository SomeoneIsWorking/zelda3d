#include "gtest/gtest.h"

#include "camera/at_default_policy.h"

using Zelda3D::CameraAtDefaultUsesExtraYBranch;

TEST(CameraAtDefaultPolicy, OrdinaryActionsLeaveSlopeFloorsToStockAccumulator) {
    EXPECT_FALSE(CameraAtDefaultUsesExtraYBranch(4, false));
    EXPECT_FALSE(CameraAtDefaultUsesExtraYBranch(7, false));
    EXPECT_FALSE(CameraAtDefaultUsesExtraYBranch(12, false));
}

TEST(CameraAtDefaultPolicy, GetItemActionUsesExtraYBranchOnSlopeFloors) {
    EXPECT_TRUE(CameraAtDefaultUsesExtraYBranch(4, true));
    EXPECT_TRUE(CameraAtDefaultUsesExtraYBranch(7, true));
    EXPECT_TRUE(CameraAtDefaultUsesExtraYBranch(12, true));
}

TEST(CameraAtDefaultPolicy, NonSlopeFloorsAlwaysUseExtraYBranch) {
    EXPECT_TRUE(CameraAtDefaultUsesExtraYBranch(0, false));
    EXPECT_TRUE(CameraAtDefaultUsesExtraYBranch(0, true));
}
