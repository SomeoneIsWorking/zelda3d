// Regression tests for the Zelda3D SDL3 GPU render fixes (terrain/world rendering on the sole backend).
//
// These lock the invariants behind three SDL3 GPU bugs that made all OoT3D world geometry invisible:
//   BUG 3 — a single 4416-byte pushed UBO exceeded SDL3 GPU's 4096-byte per-push descriptor range,
//           so every field past offset 4096 (lighting/fog/tint/skin-enable) read 0 -> black + T-pose.
//   BUG 2 — front-face winding: OoT3D geometry is CCW; the default must select CCW-front or all
//           single-sided world geometry (terrain, sky dome) is back-culled.
// The std140 offset checks also guard against a field reorder silently desyncing the C struct from
// the shader's UBO block.

#include "gtest/gtest.h"
#include "fast/zelda3d_sg_ubo.h"

using namespace Zelda3DSg;

// BUG 3: neither pushed uniform block may exceed SDL3 GPU's MAX_UBO_SECTION_SIZE. If this fails, the
// renderer silently reads 0 for everything past the cap -> black world, T-posed actors.
TEST(Zelda3DUboLayout, PushBlocksFitSdl3GpuSectionCap) {
    EXPECT_LE(kCommonBytes, kMaxUboSectionBytes);
    EXPECT_LE(kBonesBytes, kMaxUboSectionBytes);
}

// The two blocks together must cover the whole struct with no gap/overlap: COMMON is [0, kCommonBytes)
// and BONES is the contiguous tail, so a single memcpy of SgUbo feeds both pushes by offset.
TEST(Zelda3DUboLayout, BlocksTileTheStructContiguously) {
    EXPECT_EQ(kCommonBytes + kBonesBytes, sizeof(SgUbo));
    EXPECT_EQ(kCommonBytes, offsetof(SgUbo, uBones));
    // uBones is the LAST member (its block is the tail) — bones occupy exactly kBonesBytes.
    EXPECT_EQ(kBonesBytes, sizeof(SgUbo::uBones));
}

// The bone array alone is the field that pushed the combined block over 4096. Confirm it is exactly
// at (not over) the cap for the supported 64-bone configuration, documenting why it gets its own
// block: 64 bones * 64 bytes/mat4 == 4096.
TEST(Zelda3DUboLayout, BoneBlockIsExactlyTheSectionCapAt64Bones) {
    EXPECT_EQ(kBonesBytes, (uint32_t)ZELDA3D_GL_MAX_BONES * 16 * sizeof(float));
    EXPECT_LE((uint32_t)ZELDA3D_GL_MAX_BONES * 16 * sizeof(float), kMaxUboSectionBytes);
}

// std140 offsets of every COMMON field must match what the shader's UBO block computes. All fields
// are vec4/mat4 (16-byte aligned) so C offsets == std140 offsets; this catches an accidental reorder.
TEST(Zelda3DUboLayout, CommonFieldOffsetsMatchStd140) {
    EXPECT_EQ(offsetof(SgUbo, uMP), 0u);
    EXPECT_EQ(offsetof(SgUbo, uMV), 64u);
    EXPECT_EQ(offsetof(SgUbo, uLightDir), 128u);
    EXPECT_EQ(offsetof(SgUbo, uParams), 144u);
    EXPECT_EQ(offsetof(SgUbo, uTintSkin), 160u);
    EXPECT_EQ(offsetof(SgUbo, uExtra), 176u);
    EXPECT_EQ(offsetof(SgUbo, uLightVP), 192u);
    EXPECT_EQ(offsetof(SgUbo, uShadow), 256u);
    EXPECT_EQ(offsetof(SgUbo, uFog), 272u);
    EXPECT_EQ(offsetof(SgUbo, uFog2), 288u);
    EXPECT_EQ(offsetof(SgUbo, uAmbient), 304u);
    EXPECT_EQ(offsetof(SgUbo, uMatConst), 320u);
    EXPECT_EQ(offsetof(SgUbo, uSheen), 336u);
    EXPECT_EQ(offsetof(SgUbo, uTex1Xf), 352u);
    EXPECT_EQ(offsetof(SgUbo, uBones), 368u);
}

// The skin-enable flag and shade tint live in uTintSkin (offset 160) — comfortably inside the COMMON
// block. This is the field whose truncation produced the black/T-pose symptom; assert it is reachable
// (i.e. fully within the pushed COMMON range), which is the property the split exists to guarantee.
TEST(Zelda3DUboLayout, TintSkinIsWithinPushedCommonRange) {
    EXPECT_LE(offsetof(SgUbo, uTintSkin) + sizeof(SgUbo::uTintSkin), kCommonBytes);
}

// BUG 2: with the default face-cull flip (gZelda3dFaceCullFlip == 0), front faces must be CCW, matching
// OoT3D's winding. CW-front (the old default) back-culls terrain and the sky dome.
TEST(Zelda3DWinding, DefaultIsCounterClockwiseFront) {
    EXPECT_FALSE(FrontFaceIsCW(/*faceCullFlip=*/0)); // default -> CCW front
    EXPECT_TRUE(FrontFaceIsCW(/*faceCullFlip=*/1));  // explicit override flips to CW
}
