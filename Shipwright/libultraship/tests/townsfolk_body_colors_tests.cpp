// Close-test for the EnHy townsfolk per-type body-color override lookup (Step 2c-a).
//
// Ground truth: EnHy_Draw (oot3d-decomp/build/decomp/001b4944.c) writes two per-type
// per-material CONSTANT-color overrides. For type N (Actor::params & 0x7f), it calls:
//     Model_SetMaterialConstantColor(model, matA, 4, colorA)  // matA constant 4 = colorA
//     Model_SetMaterialConstantColor(model, matB, 3, colorB)  // matB constant 3 = colorB
// with (matA, matB, colorA, colorB) read from the OoT3D binary at 0x00527704 (stride 0x28),
// vendored in-tree as Shipwright/soh/src/zelda3d/behaviors/actor/enhy_body_colors.inc.
//
// This test locks the port's lookup helper against the vendored table so a future
// re-extraction (or a wire-up regression that reads the wrong row) surfaces as a
// concrete failure with a named divergence.
//
// Types are picked to cover:
//   - a "no override" row (matA=matB=-1 → helper returns 0 overrides)
//   - a BOB row with a distinct non-white colorB (type 3 blue-tinted dress)
//   - a BOB row with a distinct non-white colorA (type 5 dark-green dress)
// The exact colors come from the extracted table (see the .inc file and
// tools/dump_enhy_body_table.py --human output).

#include "gtest/gtest.h"
#include "actor/townsfolk_body_colors.h"

using Zelda3D::TownsfolkBodyColorOverrides;
using Zelda3D::TownsfolkMatConstOverride;

// Type 0 in the table has matA=matB=-1 (row uses default palette, no override).
// The helper must return 0 so the caller doesn't apply any spurious writes.
TEST(TownsfolkBodyColors, Type0NoOverrideReturnsZero) {
    TownsfolkMatConstOverride ov[2];
    EXPECT_EQ(TownsfolkBodyColorOverrides(0, ov), 0);
}

// Type 3 in the table (OBJECT_BOB, blue-tinted dress): matA=0 colorA=(1,1,1,0);
// matB=1 colorB=(0.216..., 0.216..., 1, 0). EnHy_Draw's semantics: colorA lands on
// (matA, constant 4), colorB on (matB, constant 3).
TEST(TownsfolkBodyColors, Type3BobBlueDressTargetsMat0Const4AndMat1Const3) {
    TownsfolkMatConstOverride ov[2];
    ASSERT_EQ(TownsfolkBodyColorOverrides(3, ov), 2);

    EXPECT_EQ(ov[0].matIdx, 0);
    EXPECT_EQ(ov[0].constIdx, 4);
    EXPECT_NEAR(ov[0].rgba[0], 1.0f, 1e-3f);
    EXPECT_NEAR(ov[0].rgba[1], 1.0f, 1e-3f);
    EXPECT_NEAR(ov[0].rgba[2], 1.0f, 1e-3f);

    EXPECT_EQ(ov[1].matIdx, 1);
    EXPECT_EQ(ov[1].constIdx, 3);
    EXPECT_NEAR(ov[1].rgba[0], 0.216f, 1e-2f);
    EXPECT_NEAR(ov[1].rgba[1], 0.216f, 1e-2f);
    EXPECT_NEAR(ov[1].rgba[2], 1.000f, 1e-2f);
}

// Type 5 in the table (OBJECT_BOB, dark-green dress): matA=0 colorA=(0.196, 0.314, 0, 0).
// This confirms the helper reads the correct row for a DIFFERENT type — a single-row
// off-by-one in the port would silently swap type 3 <-> 5's palettes.
TEST(TownsfolkBodyColors, Type5BobDarkGreenTargetsMat0Const4) {
    TownsfolkMatConstOverride ov[2];
    ASSERT_EQ(TownsfolkBodyColorOverrides(5, ov), 2);

    EXPECT_EQ(ov[0].matIdx, 0);
    EXPECT_EQ(ov[0].constIdx, 4);
    EXPECT_NEAR(ov[0].rgba[0], 0.196f, 1e-2f);
    EXPECT_NEAR(ov[0].rgba[1], 0.314f, 1e-2f);
    EXPECT_NEAR(ov[0].rgba[2], 0.000f, 1e-2f);

    EXPECT_EQ(ov[1].matIdx, 1);
    EXPECT_EQ(ov[1].constIdx, 3);
}

// Bounds: type outside the table's [0..21] range must not deref the table.
TEST(TownsfolkBodyColors, OutOfRangeTypeReturnsZero) {
    TownsfolkMatConstOverride ov[2];
    EXPECT_EQ(TownsfolkBodyColorOverrides(-1, ov), 0);
    EXPECT_EQ(TownsfolkBodyColorOverrides(22, ov), 0);
    EXPECT_EQ(TownsfolkBodyColorOverrides(0x1000, ov), 0);
}
