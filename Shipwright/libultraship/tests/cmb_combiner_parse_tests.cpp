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
#include "asset/cmb_glgroups.h"
#include "asset/zar.h"
#include "asset/ctr_rom.h"
#include "fast/zelda3d_sampler.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using Zelda3D::Cmb;
using Zelda3D::CmbMaterial;
using Zelda3D::CtrRom;
using Zelda3D::Zar;

TEST(Zelda3DSampler, ResolvesCmbMinificationEnumsWithoutInventingMipSelection) {
    using Fast::ResolveZelda3DSamplerFilter;
    using Fast::Zelda3DMipmapFilter;
    using Fast::Zelda3DTextureFilter;

    const auto linear = ResolveZelda3DSamplerFilter(0x2601, 0x2601);
    EXPECT_EQ(linear.minification, Zelda3DTextureFilter::Linear);
    EXPECT_EQ(linear.magnification, Zelda3DTextureFilter::Linear);
    EXPECT_EQ(linear.mipmap, Zelda3DMipmapFilter::None);

    EXPECT_EQ(ResolveZelda3DSamplerFilter(0x2701, 0x2601).mipmap, Zelda3DMipmapFilter::Nearest);
    EXPECT_EQ(ResolveZelda3DSamplerFilter(0x2703, 0x2601).mipmap, Zelda3DMipmapFilter::Linear);

    const auto nearestLinearMip = ResolveZelda3DSamplerFilter(0x2702, 0x2600);
    EXPECT_EQ(nearestLinearMip.minification, Zelda3DTextureFilter::Nearest);
    EXPECT_EQ(nearestLinearMip.magnification, Zelda3DTextureFilter::Nearest);
    EXPECT_EQ(nearestLinearMip.mipmap, Zelda3DMipmapFilter::Linear);
}

namespace {

// Best-effort: locate the OoT3D ROM from the same env var the tools use, so this
// runs in the developer's ordinary shell but skips cleanly in CI without a ROM.
static std::string OoT3dRomPath() {
    const char* p = std::getenv("ZELDA3D_OOT3D_ROM");
    return p ? std::string(p) : std::string();
}

static std::vector<uint8_t> LoadCmbFromZar(const std::string& zarPath, const std::string& nameFragment) {
    CtrRom rom(OoT3dRomPath());
    Zar zar(rom.read(zarPath));
    for (const auto& file : zar.files()) {
        if (file.name.find(nameFragment) != std::string::npos) {
            return zar.read(file);
        }
    }
    return {};
}

// Load AHG's hyliaman2.cmb (the shared "Hylian man 2" body used by several EnHy types
// including AHG). AHG mat 0 is the clothing MODULATE(PRIM, TEX0)+MODULATE(PREV, CONST[4])
// two-stage material EnHy_Draw overrides via colorA on constant slot 4.
static std::vector<uint8_t> LoadAhgCmb() {
    return LoadCmbFromZar("/actor/zelda_ahg.zar", ".cmb");
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
            << "mat " << i
            << " combiner is MODULATE(PRIM, TEX0) — srcC=CONSTANT is a leftover default "
               "that MODULATE ignores; flagging it as live CONSTANT usage would darken the mesh to black";
    }
}

namespace {

// Load g_title.cmb (the title fire-glow overlay, /actor/zelda_mag.zar) — the single
// material whose full TEV chain is byte-decoded in oot3d-decomp/docs/
// title_logo_fireglow_cmab.md §3.1: stage0 = ADD_MULT(TEX0, TEX1, TEX0) dual-texture,
// stage1 = MODULATE(PREV, CONST0) at scaleRGB=x2, stage2 = passthrough.
static std::vector<uint8_t> LoadTitleGlowCmb() {
    return LoadCmbFromZar("/actor/zelda_mag.zar", "g_title.cmb");
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
    return LoadCmbFromZar("/actor/zelda_mag.zar", "title_logo_us.cmb");
}

} // namespace

// Close-test for the exact-stage-count guard added after the original two-stage title classifier.
// Materials 4/6/7/9 have a third alpha/constant stage, so routing them through a two-stage legacy
// approximation would discard authored work. Material 8 has two stages but never consumes TEX1.
// All five therefore belong on the generic TEV evaluator.
TEST(CmbCombinerParse, TitleLogoUsShieldSwordChainsUseGenericTev) {
    if (OoT3dRomPath().empty()) {
        GTEST_SKIP() << "ZELDA3D_OOT3D_ROM not set — cannot exercise real-asset close-test";
    }
    Cmb cmb(LoadTitleLogoUsCmb());
    ASSERT_TRUE(cmb.ok()) << cmb.error();
    ASSERT_EQ(cmb.materials().size(), 12u);

    const CmbMaterial& mat4 = cmb.materials()[4];
    EXPECT_EQ(mat4.tex1_idx, 2);
    EXPECT_EQ(mat4.comb_stage_count, 3);
    EXPECT_EQ(mat4.dual_tex_mode, CmbMaterial::kDualTexNone);
    EXPECT_TRUE(mat4.tev_generic);

    const CmbMaterial& mat6 = cmb.materials()[6];
    EXPECT_EQ(mat6.tex1_idx, 5);
    EXPECT_EQ(mat6.comb_stage_count, 3);
    EXPECT_EQ(mat6.dual_tex_mode, CmbMaterial::kDualTexNone);
    EXPECT_TRUE(mat6.tev_generic);

    const CmbMaterial& mat7 = cmb.materials()[7];
    EXPECT_EQ(mat7.tex1_idx, 6);
    EXPECT_EQ(mat7.comb_stage_count, 3);
    EXPECT_EQ(mat7.dual_tex_mode, CmbMaterial::kDualTexNone);
    EXPECT_TRUE(mat7.tev_generic);

    const CmbMaterial& mat8 = cmb.materials()[8];
    EXPECT_EQ(mat8.tex1_idx, 7);
    EXPECT_EQ(mat8.comb_stage_count, 2);
    EXPECT_EQ(mat8.dual_tex_mode, CmbMaterial::kDualTexNone)
        << "mat8 declares a tex1 binding but never sources TEXTURE1 from an active combiner "
           "slot (both stages ignore it) — must NOT be classified as dual-texture";
    EXPECT_TRUE(mat8.tev_generic);

    const CmbMaterial& mat9 = cmb.materials()[9];
    EXPECT_EQ(mat9.tex1_idx, 7);
    EXPECT_EQ(mat9.comb_stage_count, 3);
    EXPECT_EQ(mat9.dual_tex_mode, CmbMaterial::kDualTexNone);
    EXPECT_TRUE(mat9.tev_generic);

    // Sphere mapping on wordmark mats 10/11 does not imply a second texture. The authored
    // chain is MODULATE(PRIMARY,TEX0) then REPLACE(PREVIOUS), and the decompiled title draw
    // only writes alpha, light, and transform state before generic submission. Exact cs1093
    // oracle identities likewise report texEn=1/0/0 for all ten matching groups.
    for (int materialIndex : { 10, 11 }) {
        const CmbMaterial& wordmark = cmb.materials()[materialIndex];
        EXPECT_EQ(wordmark.tex1_idx, -1);
        EXPECT_EQ(wordmark.comb_stage_count, 2);
        EXPECT_EQ(wordmark.coord0_mapping, 3);
        EXPECT_FLOAT_EQ(wordmark.scale_s, 1.0f);
        EXPECT_FLOAT_EQ(wordmark.scale_t, 1.0f);
        EXPECT_FLOAT_EQ(wordmark.trans_s, 0.0f);
        EXPECT_FLOAT_EQ(wordmark.trans_t, 0.0f);
        EXPECT_EQ(wordmark.dual_tex_mode, CmbMaterial::kDualTexNone);
        EXPECT_TRUE(wordmark.tev_generic);
    }
}

// CmbVShader words 112--120 seed unlit PRIMARY with MatDiffuseColor, then replace it with
// aColor only when the draw's HasColor uniform is true. The dungeon candle is a real,
// non-BossFd2 close-test: it is unlit, has no color attribute data, and authors c8 as
// (255,140,0,255). The old renderer discarded both HasColor and diffuse alpha, defaulted the
// absent vertex stream to white, and therefore could not reproduce this branch.
TEST(CmbPrimaryParse, DungeonCandlePreservesNoColorMatDiffuseFallback) {
    if (OoT3dRomPath().empty()) {
        GTEST_SKIP() << "ZELDA3D_OOT3D_ROM not set — cannot exercise real-asset close-test";
    }

    Cmb cmb(LoadCmbFromZar("/actor/zelda_dangeon_keep.zar", "efc_candle_modelT.cmb"));
    ASSERT_TRUE(cmb.ok()) << cmb.error();
    ASSERT_EQ(cmb.materials().size(), 1u);
    const auto groups = cmb.buildDrawGroups();

    const CmbMaterial& material = cmb.materials()[0];
    EXPECT_FALSE(material.vertex_lighting);
    EXPECT_FLOAT_EQ(material.mat_diffuse[0], 1.0f);
    EXPECT_FLOAT_EQ(material.mat_diffuse[1], 140.0f / 255.0f);
    EXPECT_FLOAT_EQ(material.mat_diffuse[2], 0.0f);
    EXPECT_FLOAT_EQ(material.mat_diffuse[3], 1.0f);

    ASSERT_EQ(groups.size(), 1u);
    const auto& group = groups.front();
    EXPECT_FALSE(group.has_color);

    const Zelda3DGlGroup glGroup = MakeGlGroup(cmb, group, group.verts.data(), 0);
    EXPECT_EQ(glGroup.hasColor, 0);
    for (int channel = 0; channel < 4; ++channel) {
        EXPECT_FLOAT_EQ(glGroup.matDiffuse[channel], material.mat_diffuse[channel]);
    }
}

// Words 89--110 give lit PRIMARY an independently-authored alpha: every enabled light contributes
// MatDiffuse.a * LightDiffuseColor.a, then HasColor optionally multiplies the completed RGBA value.
// The bottled Poe is a retail, non-BossFd2 close-test with no color stream, c8.a=76/255, and a TEV
// stage that consumes PRIMARY.a. The previous shader silently replaced this value with aColor.a=1.
TEST(CmbPrimaryParse, BottledPoePreservesLitNoColorDiffuseAlpha) {
    if (OoT3dRomPath().empty()) {
        GTEST_SKIP() << "ZELDA3D_OOT3D_ROM not set — cannot exercise real-asset close-test";
    }

    Cmb cmb(LoadCmbFromZar("/actor/zelda_gi_ghost.zar", "zelda_gi_ghost.cmb"));
    ASSERT_TRUE(cmb.ok()) << cmb.error();
    ASSERT_FALSE(cmb.materials().empty());
    const auto groups = cmb.buildDrawGroups();

    const CmbMaterial& material = cmb.materials()[0];
    EXPECT_TRUE(material.vertex_lighting);
    EXPECT_FLOAT_EQ(material.mat_diffuse[3], 76.0f / 255.0f);

    const auto group = std::find_if(groups.begin(), groups.end(), [](const Zelda3D::CmbDrawGroup& candidate) {
        return candidate.material_index == 0 && candidate.mesh_id == 0;
    });
    ASSERT_NE(group, groups.end());
    EXPECT_FALSE(group->has_color);

    const Zelda3DGlGroup glGroup = Zelda3D::MakeGlGroup(cmb, *group, group->verts.data(), 0);
    EXPECT_EQ(glGroup.vertexLighting, 1);
    EXPECT_EQ(glGroup.hasColor, 0);
    EXPECT_FLOAT_EQ(glGroup.matDiffuse[3], 76.0f / 255.0f);
    ASSERT_GT(glGroup.tevStageCount, 0);
    EXPECT_EQ((glGroup.tevStagePack[0][0] >> 16) & 0xFu, 0u); // PRIMARY is alpha source 0.
}

// PICA initializes both fixed-function fragment colors to zero and only calls the lighting unit
// when IsFragmentLighting is set. Dark Link deliberately authors a FRAGMENT_PRIMARY TEV source
// while leaving that flag clear, so substituting vertex PRIMARY changes the material instead of
// reproducing the disabled unit.
TEST(CmbFragmentLightingParse, DarkLinkPreservesDisabledFragmentSourceBranch) {
    if (OoT3dRomPath().empty()) {
        GTEST_SKIP() << "ZELDA3D_OOT3D_ROM not set — cannot exercise real-asset close-test";
    }

    Cmb cmb(LoadCmbFromZar("/actor/zelda_torch2.zar", "darklink.cmb"));
    ASSERT_TRUE(cmb.ok()) << cmb.error();
    ASSERT_FALSE(cmb.materials().empty());
    const CmbMaterial& material = cmb.materials()[0];
    EXPECT_FALSE(material.fragment_lighting);
    ASSERT_GT(material.comb_stage_count, 0);
    EXPECT_EQ(material.comb_stages[0].rgb_src[0], 0x6210); // FRAGMENT_PRIMARY_COLOR_DMP

    const auto groups = cmb.buildDrawGroups();
    const auto group = std::find_if(groups.begin(), groups.end(), [](const Zelda3D::CmbDrawGroup& candidate) {
        return candidate.material_index == 0;
    });
    ASSERT_NE(group, groups.end());
    const Zelda3DGlGroup glGroup = Zelda3D::MakeGlGroup(cmb, *group, group->verts.data(), 0);
    EXPECT_EQ(glGroup.fragmentLighting, 0);
    EXPECT_EQ(glGroup.tevStagePack[0][0] & 0xFu, 1u); // Packed source code 1 = fragment primary.
}

namespace {

// Load Volvagia's hole-form body. Four of its seven groups combine TEX0 with an additive
// TEX1 fire-detail layer whose coordinator explicitly selects the independent texCoord1 stream.
static std::vector<uint8_t> LoadBossFd2Cmb() {
    return LoadCmbFromZar("/actor/zelda_fd.zar", "valbasiagnd.cmb");
}

} // namespace

// Regression for the independent TEXCOORD1 path. The old parser inspected coordinator byte 1
// (referenceCamera) instead of byte 0 (sourceCoordinate), concluded every material selected
// texCoord0, and retained only one UV pair in CmbVertex/Zelda3DGlVtx. That silently sampled the
// additive fire-detail texture with the body texture's coordinates.
TEST(CmbCombinerParse, BossFd2SecondaryTextureUsesIndependentTexCoordOne) {
    if (OoT3dRomPath().empty()) {
        GTEST_SKIP() << "ZELDA3D_OOT3D_ROM not set — cannot exercise real-asset close-test";
    }
    Cmb cmb(LoadBossFd2Cmb());
    ASSERT_TRUE(cmb.ok()) << cmb.error();
    ASSERT_EQ(cmb.materials().size(), 6u);

    const CmbMaterial& bodyMaterial = cmb.materials()[1];
    EXPECT_EQ(bodyMaterial.min_filter, 0x2601);
    EXPECT_EQ(bodyMaterial.mag_filter, 0x2601);
    EXPECT_EQ(bodyMaterial.min1_filter, 0x2601);
    EXPECT_EQ(bodyMaterial.mag1_filter, 0x2601);

    for (int materialIndex : { 0, 1, 5 }) {
        EXPECT_EQ(cmb.materials()[materialIndex].coord1_source, 1)
            << "valbasiagnd material " << materialIndex << " must source its additive TEX1 stage from texCoord1";
    }

    const auto groups = cmb.buildDrawGroups();
    ASSERT_EQ(groups.size(), 7u);
    int affectedGroups = 0;
    size_t affectedVertices = 0;
    for (const auto& group : groups) {
        if (group.material_index != 0 && group.material_index != 1 && group.material_index != 5) {
            continue;
        }
        affectedGroups++;
        for (const auto& vertex : group.verts) {
            affectedVertices++;
            EXPECT_TRUE(std::fabs(vertex.uv1[0] - vertex.uv[0]) > 1e-6f ||
                        std::fabs(vertex.uv1[1] - vertex.uv[1]) > 1e-6f)
                << "every referenced valbasiagnd fire-detail coordinate is authored independently";
        }
    }
    EXPECT_EQ(affectedGroups, 4);
    // The source groups reference 598 unique vertices; buildDrawGroups expands indexed
    // triangles, so the shipping vertex buffer contains 2,136 affected vertices.
    EXPECT_EQ(affectedVertices, 2136u);

    const auto materialOne =
        std::find_if(groups.begin(), groups.end(), [](const auto& group) { return group.material_index == 1; });
    ASSERT_NE(materialOne, groups.end());
    const Zelda3DGlGroup glGroup = MakeGlGroup(cmb, *materialOne, materialOne->verts.data(), 0);
    EXPECT_EQ(glGroup.minFilter, 0x2601u);
    EXPECT_EQ(glGroup.magFilter, 0x2601u);
    EXPECT_EQ(glGroup.min1Filter, 0x2601u);
    EXPECT_EQ(glGroup.mag1Filter, 0x2601u);
}

// The PICA source register leaves one four-bit field unused between the RGB and alpha source
// triplets. This is easy to miss because the modifier register puts alpha at bit 12 instead. The
// oracle's valbasiagnd material-1 records are the close-test: their source words are
// e300430/e1f0e43/e1f0edf/e1f0eef, not the old e30430/e1fe43/e1fedf/e1feef packing.
TEST(CmbCombinerParse, BossFd2PicaSourceWordsKeepAlphaGap) {
    if (OoT3dRomPath().empty()) {
        GTEST_SKIP() << "ZELDA3D_OOT3D_ROM not set — cannot exercise real-asset close-test";
    }
    Cmb cmb(LoadBossFd2Cmb());
    ASSERT_TRUE(cmb.ok()) << cmb.error();
    const auto groups = cmb.buildDrawGroups();
    const auto it =
        std::find_if(groups.begin(), groups.end(), [](const auto& group) { return group.material_index == 1; });
    ASSERT_NE(it, groups.end());
    ASSERT_FALSE(it->verts.empty());
    const Zelda3DGlGroup glGroup = MakeGlGroup(cmb, *it, it->verts.data(), 0);
    ASSERT_EQ(glGroup.tevStageCount, 4);
    constexpr unsigned expectedSources[] = { 0x0e300430, 0x0e1f0e43, 0x0e1f0edf, 0x0e1f0eef };
    for (int stage = 0; stage < glGroup.tevStageCount; ++stage) {
        EXPECT_EQ(glGroup.tevStagePack[stage][0], expectedSources[stage]) << "stage " << stage;
    }
}

// Morpha material 0 is the strongest currently catalogued counterfactual for the PICA
// fixed-function fragment-light descriptor: it enables both fragment outputs and differs from
// the Hut default at enum_10, flag_14, and enum_26.
TEST(CmbCombinerParse, MorphaPreservesFragmentLightingDescriptor) {
    if (OoT3dRomPath().empty()) {
        GTEST_SKIP() << "ZELDA3D_OOT3D_ROM not set — cannot exercise real-asset close-test";
    }
    Cmb cmb(LoadCmbFromZar("/actor/zelda_mo.zar", "morpha.cmb"));
    ASSERT_TRUE(cmb.ok()) << cmb.error();
    ASSERT_FALSE(cmb.materials().empty());

    const CmbMaterial& material = cmb.materials()[0];
    const auto& descriptor = material.fragment_lighting_descriptor;
    EXPECT_TRUE(material.fragment_lighting);
    EXPECT_EQ(descriptor.enum_10, 0x84C2);
    EXPECT_EQ(descriptor.enum_12, 0x62C9);
    EXPECT_TRUE(descriptor.flag_14);
    EXPECT_EQ(descriptor.enum_18, 0x62B0);
    EXPECT_EQ(descriptor.enum_1c, 0x62C0);
    EXPECT_TRUE(descriptor.enabled);
    EXPECT_EQ(descriptor.enum_26, 0x62A2);
    EXPECT_FLOAT_EQ(descriptor.scale, 1.0f);
}
