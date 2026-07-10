// Close-test for the CMB PICA200 combiner parse against the real OoT3D binary.
//
// This locks two invariants that failed silently before fa23d12b:
//   (1) The per-stage CONSTANT-color selector lives at combiner-entry +0x24 as a u32,
//       NOT +0x14 (which is a source-operand field). Reading the wrong byte returned
//       constIdx=0 for AHG mat 0 stage 1 when the ground truth is 4 — the exact slot
//       EnHy_Draw overwrites via colorA per oot3d-decomp/build/decomp/001b4944.c.
//   (2) The parse must OR "sources CONSTANT" across ALL stages of a material (not just
//       stage 0), and it must consider the OP's slot arity: MODULATE / ADD / SUB /
//       DOT3 read A+B only; INTERPOLATE / MULT_ADD / ADD_SIGNED read A+B+C; REPLACE
//       reads A only. Scene room materials commonly have srcC=CONST as a leftover
//       default while running MODULATE(A,B) — treating that as a live CONSTANT
//       reference would darken the whole world to black.
//
// Both are asserted against AHG's shipped hyliaman2.cmb (the material EnHy_Draw
// overrides), which requires the real ROM. When ZELDA3D_OOT3D_ROM isn't set, the
// test SKIPS (with an explicit gtest skip so it isn't a silent pass).
//
// This is the retroactive close-test for the fa23d12b combiner-parse fix — it
// transitions RED on the previous parse (constIdx offset +0x14, single-stage
// walk, aggressive "any src == CONST") to GREEN on the corrected parse.

#include "gtest/gtest.h"
#include "asset/cmb.h"
#include "asset/zar.h"
#include "asset/ctr_rom.h"

#include <cstdlib>
#include <string>
#include <vector>

using Zelda3D::Cmb;
using Zelda3D::CmbMaterial;
using Zelda3D::Zar;
using Zelda3D::CtrRom;

namespace {

// Best-effort: locate the OoT3D ROM from the same env var the tools use, so this
// runs in the developer's ordinary shell but skips cleanly in CI without a ROM.
static std::string OoT3dRomPath() {
    const char* p = std::getenv("ZELDA3D_OOT3D_ROM");
    return p ? std::string(p) : std::string();
}

// Load AHG's hyliaman2.cmb (the shared "Hylian man 2" body used by several EnHy types
// including AHG). AHG mat 0 is the clothing MODULATE(PRIM, TEX0)+MODULATE(PREV, CONST[4])
// two-stage material EnHy_Draw overrides via colorA on constant slot 4.
static std::vector<uint8_t> LoadAhgCmb() {
    CtrRom rom(OoT3dRomPath());
    auto zar_bytes = rom.read("/actor/zelda_ahg.zar");
    Zar zar(std::move(zar_bytes));
    for (const auto& f : zar.files()) {
        if (f.name.size() >= 4 && f.name.compare(f.name.size() - 4, 4, ".cmb") == 0) {
            return zar.read(f);
        }
    }
    return {};
}

} // namespace

TEST(CmbCombinerParse, AhgHyliaman2ParsesOk) {
    if (OoT3dRomPath().empty()) {
        GTEST_SKIP() << "ZELDA3D_OOT3D_ROM not set — cannot exercise real-asset close-test";
    }
    auto cmb_bytes = LoadAhgCmb();
    ASSERT_FALSE(cmb_bytes.empty());
    Cmb cmb(std::move(cmb_bytes));
    ASSERT_TRUE(cmb.ok()) << cmb.error();
    // hyliaman2.cmb has 8 materials (verified by dumping the shipped ROM asset).
    ASSERT_EQ(cmb.materials().size(), 8u);
}

// (1) The clothing material has TWO stages; the CONSTANT-color selector for the
// stage that sources CONSTANT is 4, matching EnHy_Draw's per-type override target
// for matA. Fails RED on the pre-fa23d12b parse: that read +0x14 (which was 0x0003
// = source-operand SRC_COLOR, taken mod 8 = 3) and only walked stage 0.
TEST(CmbCombinerParse, AhgMat0ClothingConstantSelectorIsSlotFour) {
    if (OoT3dRomPath().empty()) {
        GTEST_SKIP() << "ZELDA3D_OOT3D_ROM not set — cannot exercise real-asset close-test";
    }
    Cmb cmb(LoadAhgCmb());
    ASSERT_TRUE(cmb.ok()) << cmb.error();
    const CmbMaterial& m0 = cmb.materials()[0];
    EXPECT_EQ(m0.comb_stage_count, 2);
    EXPECT_TRUE(m0.comb_uses_const)
        << "AHG mat 0 stage 1 = MODULATE(PREV, CONST) — must be flagged as sourcing CONSTANT";
    EXPECT_EQ((int)m0.comb_const_idx, 4)
        << "AHG mat 0 stage 1's CONSTANT-color selector must be slot 4 (EnHy_Draw's colorA "
           "override target); the previous parse returned 0 by reading combiner-entry +0x14 "
           "instead of +0x24";
}

// AHG mat 1 (the paired clothing material) uses stage 1 CONSTANT slot 3 (EnHy_Draw's
// matB override target). Same defect shape; different slot.
TEST(CmbCombinerParse, AhgMat1ClothingConstantSelectorIsSlotThree) {
    if (OoT3dRomPath().empty()) {
        GTEST_SKIP() << "ZELDA3D_OOT3D_ROM not set — cannot exercise real-asset close-test";
    }
    Cmb cmb(LoadAhgCmb());
    ASSERT_TRUE(cmb.ok()) << cmb.error();
    const CmbMaterial& m1 = cmb.materials()[1];
    EXPECT_EQ(m1.comb_stage_count, 2);
    EXPECT_TRUE(m1.comb_uses_const);
    EXPECT_EQ((int)m1.comb_const_idx, 3);
}

// (2) The non-clothing materials on the same body (single-stage MODULATE(PRIM, TEX0)
// with srcC=CONSTANT as a leftover default) must NOT be flagged as sourcing CONSTANT
// — otherwise the shader multiplies their fragment output by the default black
// mat_constant[0]. The previous "any src == CONST" heuristic returned true here.
TEST(CmbCombinerParse, AhgSingleStageMaterialsDoNotFlagConstant) {
    if (OoT3dRomPath().empty()) {
        GTEST_SKIP() << "ZELDA3D_OOT3D_ROM not set — cannot exercise real-asset close-test";
    }
    Cmb cmb(LoadAhgCmb());
    ASSERT_TRUE(cmb.ok()) << cmb.error();
    // Mats 2..7 on hyliaman2 are single-stage MODULATE materials (head/hands/etc).
    for (size_t i = 2; i < cmb.materials().size(); i++) {
        const CmbMaterial& m = cmb.materials()[i];
        EXPECT_EQ(m.comb_stage_count, 1) << "mat " << i << " expected single-stage";
        EXPECT_FALSE(m.comb_uses_const)
            << "mat " << i << " combiner is MODULATE(PRIM, TEX0) — srcC=CONSTANT is a leftover default "
               "that MODULATE ignores; flagging it as live CONSTANT usage would darken the mesh to black";
    }
}

namespace {

// Load g_title.cmb (the title fire-glow overlay, /actor/zelda_mag.zar) — the single
// material whose full TEV chain is byte-decoded in oot3d-decomp/docs/
// title_logo_fireglow_cmab.md §3.1: stage0 = ADD_MULT(TEX0, TEX1, TEX0) dual-texture,
// stage1 = MODULATE(PREV, CONST0) at scaleRGB=x2, stage2 = passthrough.
static std::vector<uint8_t> LoadTitleGlowCmb() {
    CtrRom rom(OoT3dRomPath());
    auto zar_bytes = rom.read("/actor/zelda_mag.zar");
    Zar zar(std::move(zar_bytes));
    for (const auto& f : zar.files()) {
        if (f.name.find("g_title.cmb") != std::string::npos) {
            return zar.read(f);
        }
    }
    return {};
}

} // namespace

// Close-test for the 2026-07-10 fire-glow combiner parse additions + the constant-color
// palette base fix. Locks four byte-verified facts about g_title.cmb material 0:
//   (a) mat_constant[0] = white (255,255,255,255). RED on the pre-fix parse, which read
//       the palette at +0xB8 (one slot late; the real base is +0xB4 per noclip
//       readMatsChunk AND a direct byte dump) and returned black — which is also why the
//       shader-side "constBlack skip" heuristic existed.
//   (b) comb_const_scale_rgb = 2.0 — stage 1's hardware RGB x2, the fire-glow
//       "half brightness" root cause (fireglow doc §3.2 fix 1).
//   (c) comb0_dual_addmult with tex1_idx=1 — stage 0's (t0+t1)*t0 detail-mask combine
//       (fix 2), sampling g_title_mable_t through binding 1.
//   (d) coordinator 1's baked UV transform scale(3,2)/trans(0,0.9433) (fix 3's target).
TEST(CmbCombinerParse, TitleGlowDualTexAddMultAndConstScale) {
    if (OoT3dRomPath().empty()) {
        GTEST_SKIP() << "ZELDA3D_OOT3D_ROM not set — cannot exercise real-asset close-test";
    }
    Cmb cmb(LoadTitleGlowCmb());
    ASSERT_TRUE(cmb.ok()) << cmb.error();
    ASSERT_EQ(cmb.materials().size(), 1u);
    const CmbMaterial& m = cmb.materials()[0];
    EXPECT_EQ(m.comb_stage_count, 3);
    // (a) palette base +0xB4: slot 0 is the CMAB-animated register, baked WHITE.
    EXPECT_FLOAT_EQ(m.mat_constant[0][0], 1.0f);
    EXPECT_FLOAT_EQ(m.mat_constant[0][1], 1.0f);
    EXPECT_FLOAT_EQ(m.mat_constant[0][2], 1.0f);
    EXPECT_FLOAT_EQ(m.mat_constant[0][3], 1.0f);
    // (b) stage 1 MODULATE(PREV, CONST0) at x2.
    EXPECT_TRUE(m.comb_uses_const);
    EXPECT_EQ((int)m.comb_const_idx, 0);
    EXPECT_FLOAT_EQ(m.comb_const_scale_rgb, 2.0f);
    // (c) stage 0 dual-texture ADD_MULT sampling binding 1.
    EXPECT_TRUE(m.comb0_dual_addmult);
    EXPECT_EQ(m.tex1_idx, 1);
    // (d) coordinator-1 baked transform.
    EXPECT_FLOAT_EQ(m.scale1_s, 3.0f);
    EXPECT_FLOAT_EQ(m.scale1_t, 2.0f);
    EXPECT_FLOAT_EQ(m.trans1_s, 0.0f);
    EXPECT_NEAR(m.trans1_t, 0.94333f, 1e-4f);
}

namespace {

// Load title_logo_us.cmb (the title wordmark model, /actor/zelda_mag.zar) — the shield/sword
// dark-square glint bug (debug_journal/2026-07-10-shield-glint-dualtex.md). Unlike g_title.cmb's
// single-stage ADD_MULT dual-texture combine, this asset's shield/sword materials spread the
// dual-texture combine across TWO combiner stages, which the pre-fix parser (only recognizing
// the single-stage ADD_MULT shape) never classified as dual-texture — and the pre-fix SgGroup
// population was ALSO gated on that single flag, so tex1 was dropped before the renderer ever
// saw it. This test locks the byte-verified classification (dual_tex_mode per material).
static std::vector<uint8_t> LoadTitleLogoUsCmb() {
    CtrRom rom(OoT3dRomPath());
    auto zar_bytes = rom.read("/actor/zelda_mag.zar");
    Zar zar(std::move(zar_bytes));
    for (const auto& f : zar.files()) {
        if (f.name.find("title_logo_us.cmb") != std::string::npos) {
            return zar.read(f);
        }
    }
    return {};
}

} // namespace

// Close-test for the 2026-07-10 multi-stage dual-texture classification (cmb.cpp
// parseMats' dual_tex_mode). Locks the byte-verified combiner shape of title_logo_us.cmb's
// shield materials (6, 7, 8, 9 — sepd 16-19) and sword material (4):
//   mat6/mat9 (shield glint dot):  stage0 ADD(TEX0,TEX1), stage1 MODULATE(PREV,PRIMARY)
//                                  -> kDualTexAddThenModulatePrimary, (t0+t1)*primary.
//   mat7      (shield sparkle):    stage0 MODULATE(PRIM,TEX0), stage1 MODULATE(PREV,TEX1) x2
//                                  -> kDualTexModulateThenScale, scale2=2.0.
//   mat4      (sword detail mask): same shape as mat7, scale2=2.0.
//   mat8      (shield, unused tex1 binding): tex1_idx >= 0 but TEXTURE1 is never an ACTIVE
//                                  combiner source in either stage -> kDualTexNone (the
//                                  detection must not fire on a merely-declared binding).
TEST(CmbCombinerParse, TitleLogoUsShieldSwordDualTexModes) {
    if (OoT3dRomPath().empty()) {
        GTEST_SKIP() << "ZELDA3D_OOT3D_ROM not set — cannot exercise real-asset close-test";
    }
    Cmb cmb(LoadTitleLogoUsCmb());
    ASSERT_TRUE(cmb.ok()) << cmb.error();
    ASSERT_EQ(cmb.materials().size(), 12u);

    const CmbMaterial& mat4 = cmb.materials()[4];
    EXPECT_EQ(mat4.tex1_idx, 2);
    EXPECT_EQ(mat4.dual_tex_mode, CmbMaterial::kDualTexModulateThenScale);
    EXPECT_FLOAT_EQ(mat4.dual_tex_scale2, 2.0f);

    const CmbMaterial& mat6 = cmb.materials()[6];
    EXPECT_EQ(mat6.tex1_idx, 5);
    EXPECT_EQ(mat6.dual_tex_mode, CmbMaterial::kDualTexAddThenModulatePrimary);

    const CmbMaterial& mat7 = cmb.materials()[7];
    EXPECT_EQ(mat7.tex1_idx, 6);
    EXPECT_EQ(mat7.dual_tex_mode, CmbMaterial::kDualTexModulateThenScale);
    EXPECT_FLOAT_EQ(mat7.dual_tex_scale2, 2.0f);

    const CmbMaterial& mat8 = cmb.materials()[8];
    EXPECT_EQ(mat8.tex1_idx, 7);
    EXPECT_EQ(mat8.dual_tex_mode, CmbMaterial::kDualTexNone)
        << "mat8 declares a tex1 binding but never sources TEXTURE1 from an active combiner "
           "slot (both stages ignore it) — must NOT be classified as dual-texture";

    const CmbMaterial& mat9 = cmb.materials()[9];
    EXPECT_EQ(mat9.tex1_idx, 7);
    EXPECT_EQ(mat9.dual_tex_mode, CmbMaterial::kDualTexAddThenModulatePrimary);
}
