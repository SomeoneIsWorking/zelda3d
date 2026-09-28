// Tests for the shared environment-light window blend.
//
// The window is the part of "fog" that decides the CURVE -- the 128-entry table PICA samples -- and it
// is fully determined by (zFar, fogNear, fogFar, cameraNear). MM3D's recovered record supplies the
// first three, and its window PREDICTS MM3D's own authored PICA LUT to 1.19 byte steps against a
// shuffled-LUT control of 0.02117 (`tools/mm3d_fog_prediction.py`). So the rule under test here is
// not "does the window look right" -- it is "does this function produce exactly the window the
// validation assumed, and refuse rather than guess when it cannot".
//
// The refusal cases carry as much weight as the arithmetic. Every one of them -- no captured schedule,
// an index past the palette, a window whose far is at or before its near, a non-positive far plane --
// is a distinct state that must end with the fog OFF rather than a plausible ramp. A window that
// renders is worse than no window, because it is indistinguishable from a correct one.

#include <gtest/gtest.h>

#include <utility>
#include <vector>

extern "C" {
#include "lighting/zelda3d_env_blend.h"
}

namespace {

/// A palette that OWNS its slots.
///
/// The first version of this returned a `Zelda3dSceneLight` holding `vector::data()` from a vector
/// that died with the return expression, so the function read freed memory and returned 20241 and
/// 3e24 instead of 25 and 125 -- plausible-looking garbage rather than a crash. Keeping the storage
/// in the palette makes the lifetime part of the type.
class Palette {
  public:
    explicit Palette(std::vector<Zelda3dLightSlot> slots) : mSlots(std::move(slots)) {
        mScene.numSlots = static_cast<unsigned char>(mSlots.size());
        mScene.slots = mSlots.data();
    }
    const Zelda3dSceneLight* get() const {
        return &mScene;
    }

  private:
    std::vector<Zelda3dLightSlot> mSlots;
    Zelda3dSceneLight mScene{};
};

Zelda3dLightSlot MakeSlot(float fogNear, float fogFar, float zFar) {
    Zelda3dLightSlot slot{};
    slot.fogNear = static_cast<unsigned short>(fogNear);
    slot.fogFar = fogFar;
    slot.zFar = zFar;
    return slot;
}

void SetSchedule(int a, int b, int c, int d, float wTime, float wConfig) {
    gZelda3dEnvBlend.valid = 1;
    gZelda3dEnvBlend.timeBased = 1;
    gZelda3dEnvBlend.idx[0] = static_cast<unsigned char>(a);
    gZelda3dEnvBlend.idx[1] = static_cast<unsigned char>(b);
    gZelda3dEnvBlend.idx[2] = static_cast<unsigned char>(c);
    gZelda3dEnvBlend.idx[3] = static_cast<unsigned char>(d);
    gZelda3dEnvBlend.wTime = wTime;
    gZelda3dEnvBlend.wConfig = wConfig;
}

class EnvBlendWindow : public ::testing::Test {
  protected:
    void SetUp() override {
        gZelda3dEnvBlend = Zelda3dEnvBlend{};
    }
};

TEST_F(EnvBlendWindow, AppliesTheN64RuleOncePerField) {
    // Two time-pairs, each lerped by wTime, then the pair lerped by wConfig. With a single index
    // repeated in both pairs the whole thing collapses to a plain lerp, which makes the expected value
    // obvious by hand and pins the ORDER of the two lerps.
    const Palette palette({ MakeSlot(0, 100, 1000), MakeSlot(100, 200, 2000) });
    SetSchedule(0, 1, 0, 1, 0.25f, 0.5f);

    float fogNear = 0.0f;
    float fogFar = 0.0f;
    float zFar = 0.0f;
    ASSERT_TRUE(Zelda3D_EnvBlendWindow(palette.get(), &fogNear, &fogFar, &zFar, nullptr));
    EXPECT_FLOAT_EQ(fogNear, 25.0f); // lerp(0, 100, 0.25) == 25
    EXPECT_FLOAT_EQ(fogFar, 125.0f); // lerp(100, 200, 0.25) == 125
    EXPECT_FLOAT_EQ(zFar, 1250.0f);
}

TEST_F(EnvBlendWindow, TheConfigWeightBlendsBetweenTheTwoTimePairs) {
    // Every slot carries a real fogFar: a palette whose far is at or before its near is DEGENERATE and
    // the function refuses it, which is the behaviour the next test pins. The first version of this
    // test used fogFar 0 as a placeholder and was correctly refused.
    const Palette palette({ MakeSlot(0, 100, 1000), MakeSlot(100, 200, 2000), MakeSlot(200, 300, 3000) });
    // wTime 0: the first pair is slot 0 and the second is slot 2. wConfig 0.5 averages them.
    SetSchedule(0, 0, 2, 2, 0.0f, 0.5f);
    float fogNear = 0.0f;
    float fogFar = 0.0f;
    float zFar = 0.0f;
    ASSERT_TRUE(Zelda3D_EnvBlendWindow(palette.get(), &fogNear, &fogFar, &zFar, nullptr));
    EXPECT_FLOAT_EQ(fogNear, 100.0f); // lerp(0, 200, 0.5)
    EXPECT_FLOAT_EQ(fogFar, 200.0f);  // lerp(100, 300, 0.5)
    EXPECT_FLOAT_EQ(zFar, 2000.0f);   // lerp(1000, 3000, 0.5)
}

TEST_F(EnvBlendWindow, RefusesWithoutACapturedSchedule) {
    const Palette palette({ MakeSlot(0, 100, 1000), MakeSlot(100, 200, 2000) });
    float near = 0.0f;
    float far = 0.0f;
    float zfar = 0.0f;
    // `valid` left 0: an uncaptured frame must not be treated as wTime 0, which would light every
    // scene from slot 0's palette instead of reporting that the schedule is unknown.
    EXPECT_FALSE(Zelda3D_EnvBlendWindow(palette.get(), &near, &far, &zfar, nullptr));
}

TEST_F(EnvBlendWindow, RefusesAnIndexPastThePalette) {
    // The palette is 2 slots and the schedule names slot 3. Reading it would be a memory error, and
    // silently clamping to slot 0 would be a plausible wrong fog.
    const Palette palette({ MakeSlot(0, 100, 1000), MakeSlot(100, 200, 2000) });
    SetSchedule(0, 3, 0, 1, 0.0f, 0.0f);
    float near = 0.0f;
    float far = 0.0f;
    float zfar = 0.0f;
    EXPECT_FALSE(Zelda3D_EnvBlendWindow(palette.get(), &near, &far, &zfar, nullptr));
}

TEST_F(EnvBlendWindow, RefusesADegenerateWindowRatherThanRampingTheWrongWay) {
    // far at or before near: the game can produce one at a transition boundary, and feeding it on
    // would invert the fog ramp -- which still renders, so nothing downstream would complain.
    const Palette inverted({ MakeSlot(200, 100, 1000) });
    SetSchedule(0, 0, 0, 0, 0.0f, 0.0f);
    float near = 0.0f;
    float far = 0.0f;
    float zfar = 0.0f;
    EXPECT_FALSE(Zelda3D_EnvBlendWindow(inverted.get(), &near, &far, &zfar, nullptr));

    const Palette flat({ MakeSlot(100, 100, 1000) });
    EXPECT_FALSE(Zelda3D_EnvBlendWindow(flat.get(), &near, &far, &zfar, nullptr));

    const Palette noFar({ MakeSlot(0, 100, 0.0f) });
    EXPECT_FALSE(Zelda3D_EnvBlendWindow(noFar.get(), &near, &far, &zfar, nullptr));
}

TEST_F(EnvBlendWindow, RefusesAnEmptyOrAbsentPalette) {
    SetSchedule(0, 0, 0, 0, 0.0f, 0.0f);
    const Zelda3dSceneLight empty{ 0, nullptr };
    float near = 0.0f;
    float far = 0.0f;
    float zfar = 0.0f;
    EXPECT_FALSE(Zelda3D_EnvBlendWindow(&empty, &near, &far, &zfar, nullptr));
    EXPECT_FALSE(Zelda3D_EnvBlendWindow(nullptr, &near, &far, &zfar, nullptr));
}

TEST_F(EnvBlendWindow, ClampsWeightsRatherThanExtrapolating) {
    // A weight outside [0,1] at a transition end would extrapolate the window into a ramp that still
    // renders. Clamping is the difference between a bounded curve and an invented one.
    const Palette palette({ MakeSlot(0, 100, 1000), MakeSlot(100, 200, 2000) });
    SetSchedule(0, 1, 0, 1, 5.0f, -3.0f);
    float fogNear = 0.0f;
    float fogFar = 0.0f;
    float zFar = 0.0f;
    ASSERT_TRUE(Zelda3D_EnvBlendWindow(palette.get(), &fogNear, &fogFar, &zFar, nullptr));
    EXPECT_FLOAT_EQ(fogNear, 100.0f); // wTime clamped to 1 -> slot 1's fogNear
    EXPECT_FLOAT_EQ(fogFar, 200.0f);
}

TEST_F(EnvBlendWindow, ReproducesTheRecoveredMM3DLostWoodsWindow) {
    // The window the MM3D fog validation actually used: z2_lost_woods slot 2, whose authored-window
    // prediction is what reproduces MM3D's own PICA LUT to 1.19 byte steps. Pinned here so a change
    // to the rule shows up as a TEST failure rather than as fog that is quietly wrong in the product.
    const Palette palette({ MakeSlot(40, 12800, 12800) });
    SetSchedule(0, 0, 0, 0, 0.0f, 0.0f);
    float fogNear = 0.0f;
    float fogFar = 0.0f;
    float zFar = 0.0f;
    ASSERT_TRUE(Zelda3D_EnvBlendWindow(palette.get(), &fogNear, &fogFar, &zFar, nullptr));
    EXPECT_FLOAT_EQ(fogNear, 40.0f);
    EXPECT_FLOAT_EQ(fogFar, 12800.0f);
    EXPECT_FLOAT_EQ(zFar, 12800.0f);
}

} // namespace
