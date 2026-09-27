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
#include "fast/backends/unified_shader.h"
#include "fast/zelda3d_instrumentation.h"
#include "fast/zelda3d_sdl3gpu_shaders.h"
#include "fast/zelda3d_sg_ubo.h"
#include "fast/backends/zelda3d_tev_glsl.h"
#include "fast/unified_ubo.h"

using namespace Zelda3DSg;

TEST(Zelda3DShaderTemplate, ExpandsEveryRepeatedVaryingQualifier) {
    std::string vertexSource;
    std::string fragmentSource;
    std::string error;
    ASSERT_TRUE(Fast::Zelda3DSdl3GpuShaders::BuildSources("", "", "", vertexSource, fragmentSource, error)) << error;
    EXPECT_EQ(vertexSource.find("{{"), std::string::npos);
    EXPECT_EQ(fragmentSource.find("{{"), std::string::npos);

    const auto count = [](const std::string& source, const std::string& token) {
        std::size_t occurrences = 0;
        std::size_t position = 0;
        while ((position = source.find(token, position)) != std::string::npos) {
            ++occurrences;
            position += token.size();
        }
        return occurrences;
    };
    EXPECT_EQ(count(vertexSource, ") out "), 8u);
    EXPECT_EQ(count(fragmentSource, ") in "), 8u);
}

TEST(Zelda3DShaderTemplate, UnlitPrimarySelectsMatDiffuseOnlyWhenColorIsAbsent) {
    std::string vertexSource;
    std::string fragmentSource;
    std::string error;
    ASSERT_TRUE(Fast::Zelda3DSdl3GpuShaders::BuildSources("", "", "", vertexSource, fragmentSource, error)) << error;
    EXPECT_NE(vertexSource.find("else if (ubo.uPrimaryCtl.x > 0.5)"), std::string::npos);
    EXPECT_NE(vertexSource.find("vPrim = min(abs(aColor), vec4(1.0))"), std::string::npos);
    EXPECT_NE(vertexSource.find("vPrim = min(abs(ubo.uMatDiffuse), vec4(1.0))"), std::string::npos);
}

TEST(Zelda3DShaderTemplate, LitPrimaryPreservesDiffuseAlphaAndOptionalColorBranch) {
    std::string vertexSource;
    std::string fragmentSource;
    std::string error;
    ASSERT_TRUE(Fast::Zelda3DSdl3GpuShaders::BuildSources("", "", "", vertexSource, fragmentSource, error)) << error;
    EXPECT_NE(vertexSource.find("float litAlpha = ubo.uLitDif1.a + ubo.uLitDif2.a"), std::string::npos);
    EXPECT_NE(vertexSource.find("vec4 primary = vec4(lit, litAlpha)"), std::string::npos);
    EXPECT_NE(vertexSource.find("if (ubo.uPrimaryCtl.x > 0.5) primary *= aColor"), std::string::npos);
    EXPECT_EQ(vertexSource.find("vec4(lit * aColor.rgb, aColor.a)"), std::string::npos);
}

TEST(Zelda3DTev, UsesPicaAlphaSourceFieldLayout) {
    const std::string source = Fast::Zelda3DTev::kGenericFunctions;
    EXPECT_NE(source.find("(w.x >> 16) & 15u"), std::string::npos);
    EXPECT_NE(source.find("(w.x >> 20) & 15u"), std::string::npos);
    EXPECT_NE(source.find("(w.x >> 24) & 15u"), std::string::npos);
    EXPECT_EQ(source.find("(w.x >> 12) & 15u"), std::string::npos);
}

TEST(Zelda3DTev, KeepsVertexAndFixedFunctionFragmentSourcesDistinct) {
    const std::string source = Fast::Zelda3DTev::kGenericFunctions;
    EXPECT_NE(source.find("if (code == 0u) return prim"), std::string::npos);
    EXPECT_NE(source.find("if (code == 1u) return fragPrimary"), std::string::npos);
    EXPECT_NE(source.find("if (code == 2u) return fragSecondary"), std::string::npos);
    EXPECT_EQ(source.find("if (code == 0u || code == 1u) return prim"), std::string::npos);
}

TEST(Zelda3DShaderTemplate, DisabledFragmentLightingSuppliesZeroTevSources) {
    std::string vertexSource;
    std::string fragmentSource;
    std::string error;
    ASSERT_TRUE(Fast::Zelda3DSdl3GpuShaders::BuildSources("", "", "", vertexSource, fragmentSource, error)) << error;
    EXPECT_NE(fragmentSource.find("ubo.uPrimaryCtl.y > 0.5 ? prim : vec4(0.0)"), std::string::npos);
    EXPECT_NE(fragmentSource.find("tevRun(prim, fragPrimary, fragSecondary"), std::string::npos);
}

TEST(Zelda3DDrawIsolation, SkipComposesWithExistingDrawAndModelSelection) {
    gZelda3dSgModelOnly = -1;
    gZelda3dSgDrawOnly = -1;
    gZelda3dSgDrawSkip = -1;
    EXPECT_TRUE(Zelda3D_SgDrawIsolationIncludes(23, 17));

    gZelda3dSgModelOnly = 23;
    EXPECT_TRUE(Zelda3D_SgDrawIsolationIncludes(23, 17));
    EXPECT_FALSE(Zelda3D_SgDrawIsolationIncludes(24, 17));

    gZelda3dSgDrawOnly = 17;
    EXPECT_TRUE(Zelda3D_SgDrawIsolationIncludes(23, 17));
    EXPECT_FALSE(Zelda3D_SgDrawIsolationIncludes(23, 18));

    gZelda3dSgDrawSkip = 17;
    EXPECT_FALSE(Zelda3D_SgDrawIsolationIncludes(23, 17));

    gZelda3dSgModelOnly = -1;
    gZelda3dSgDrawOnly = -1;
    gZelda3dSgDrawSkip = -1;
}

// CmbVShader's PRIMARY path dots the transformed/skinned normal against both actor light slots.
// The unified CMB route once used one model-space NdotL and silently ignored the second slot even
// though its UBO already mirrored the native fields. Lock the shipping shader source to the full
// bank so later UBO cleanup cannot regress actor lighting to a single-light approximation.
TEST(Zelda3DUnifiedShader, CmbPrimaryUsesTransformedNormalAndBothLightSlots) {
    const std::string source = Fast::Unified::BuildVertexSource(Fast::Unified::Variant::kGenericTev);
    EXPECT_NE(source.find("vec3 nV = normalize(vNrmView);"), std::string::npos);
    EXPECT_NE(source.find("ubo.uLitDif1.rgb * max(dot(nV, -ubo.uLightDir.xyz), 0.0)"), std::string::npos);
    EXPECT_NE(source.find("ubo.uLitDif2.rgb * max(dot(nV, -ubo.uLightDir2.xyz), 0.0)"), std::string::npos);
    EXPECT_NE(source.find("float litAlpha = ubo.uLitDif1.a + ubo.uLitDif2.a"), std::string::npos);
    EXPECT_NE(source.find("if (ubo.uPrimaryCtl.x > 0.5) primary *= aColor0"), std::string::npos);
    EXPECT_EQ(source.find("dot(normalize(nM), -normalize(ubo.uLightDir.xyz))"), std::string::npos);
}

TEST(Zelda3DUnifiedShader, DisabledFragmentLightingSuppliesZeroTevSources) {
    const std::string source = Fast::Unified::BuildFragmentSource(Fast::Unified::Variant::kGenericTev);
    EXPECT_NE(source.find("ubo.uPrimaryCtl.y > 0.5 ? vColor0 : vec4(0.0)"), std::string::npos);
    EXPECT_NE(source.find("tevRun(vColor0, fragPrimary, fragSecondary"), std::string::npos);
}

TEST(Zelda3DUnifiedShader, UnlitPrimarySelectsMatDiffuseOnlyWhenColorIsAbsent) {
    const std::string source = Fast::Unified::BuildVertexSource(Fast::Unified::Variant::kGenericTev);
    EXPECT_NE(source.find("ubo.uPrimaryCtl.x < 0.5"), std::string::npos);
    EXPECT_NE(source.find("? ubo.uMatDiffuse"), std::string::npos);
    EXPECT_NE(source.find(": aColor0"), std::string::npos);
}

// The title logo is force-unlit with respect to the scene, but its actor draw binds a private
// CmbVShader light. When CMB draws moved to the unified generic-TEV route, lightingMode stayed zero
// and the copied uSheen payload was never consumed, making the wordmark render at raw texture
// brightness. Lock the independently-gated RE expression into the vertex-side PRIMARY producer.
TEST(Zelda3DUnifiedShader, ForceUnlitWordmarkStillAppliesPrivateSheenToPrimary) {
    const std::string source = Fast::Unified::BuildVertexSource(Fast::Unified::Variant::kGenericTev);
    const auto sheenGate = source.find("if (ubo.uSheen.x > 0.0)");
    const auto worldLightGate = source.find("else if (ubo.uParams0.y > 1.5)");
    ASSERT_NE(sheenGate, std::string::npos);
    ASSERT_NE(worldLightGate, std::string::npos);
    EXPECT_LT(sheenGate, worldLightGate);
    EXPECT_NE(source.find("ubo.uSheen.x + ndotl"), std::string::npos);
    EXPECT_NE(source.find("dot(nV, -normalize(ubo.uLightDir.xyz))"), std::string::npos);
}

// The unified ownership boundary must preserve all state the native CMB path already consumed:
// the oracle CmbVShader sphere-normal matrix and the three byte-classified dual-texture formulas. These
// source checks falsify the exact regression where the UBO fields were copied but ignored.
TEST(Zelda3DUnifiedShader, CmbDualTextureVariantConsumesSphereNormalMatrixAndLegacyModes) {
    const std::string vertex = Fast::Unified::BuildVertexSource(Fast::Unified::Variant::kDualTex);
    const std::string fragment = Fast::Unified::BuildFragmentSource(Fast::Unified::Variant::kDualTex);
    EXPECT_NE(vertex.find("ubo.uSphNrm0.w > 0.5"), std::string::npos);
    EXPECT_NE(vertex.find("dot(ubo.uSphNrm0.xyz, nM)"), std::string::npos);
    EXPECT_NE(fragment.find("t0.rgb * t1 * ubo.uSheen.z"), std::string::npos);
    EXPECT_NE(fragment.find("clamp(t0.rgb + t1"), std::string::npos);
    EXPECT_EQ(fragment.find("t0s * ubo.uSheen.z + t1"), std::string::npos);
}

// CMB texture coordinators are independent. title_logo_us mats 10/11 request
// CameraSphereEnvMap on coordinator 0 while leaving coordinator 1 disabled; routing the mapping
// through the old dual-texture flag invented a second sampler and a non-authored 3x combine.
TEST(Zelda3DUnifiedShader, CmbPrimarySphereMapUsesCoordinatorZeroState) {
    const std::string vertex = Fast::Unified::BuildVertexSource(Fast::Unified::Variant::kGenericTev);
    EXPECT_NE(vertex.find("ubo.uSheen.w > 2.5"), std::string::npos);
    EXPECT_NE(vertex.find("ubo.uTex0Xf.z"), std::string::npos);
    EXPECT_NE(vertex.find("ubo.uTex0Xf.x"), std::string::npos);
    EXPECT_NE(vertex.find("ubo.uTevCtl.y > 2.5"), std::string::npos);
}

TEST(Zelda3DUnifiedShader, SelectedFragmentProbesConsumeTheUnifiedDebugGate) {
    using Fast::Unified::Variant;
    const std::string tex0 = Fast::Unified::BuildFragmentSource(Variant::kGenericTev, 1);
    const std::string primary = Fast::Unified::BuildFragmentSource(Variant::kGenericTev, 5);
    const std::string combined = Fast::Unified::BuildFragmentSource(Variant::kGenericTev, 6);
    EXPECT_NE(tex0.find("ubo.uDebug.x > 0.5"), std::string::npos);
    EXPECT_NE(tex0.find("fragColor = vec4(texel0().rgb, 1.0)"), std::string::npos);
    EXPECT_NE(primary.find("fragColor = vec4(vColor0.rgb, 1.0)"), std::string::npos);
    EXPECT_NE(combined.find("fragColor = vec4(texel.rgb, 1.0)"), std::string::npos);
    EXPECT_EQ(Fast::Unified::BuildFragmentSource(Variant::kGenericTev).find("ubo.uDebug.x > 0.5"), std::string::npos);
}

TEST(Zelda3DUnifiedShader, CmbDrawModulationAppliesToEveryCmbDrawExceptVertexLitAndN64) {
    const std::string vertex = Fast::Unified::BuildVertexSource(Fast::Unified::Variant::kGenericTev);
    const std::string fragment = Fast::Unified::BuildFragmentSource(Fast::Unified::Variant::kGenericTev);
    // Exactly two exclusions: an N64 already-transformed draw (uPrimColor is a combiner SOURCE
    // there, so multiplying would double-apply) and the vertex-lit mode (PRIMARY comes from the
    // PICA light bank).
    EXPECT_NE(vertex.find("if (ubo.uParams1.w < 0.5 && ubo.uParams0.y < 1.5)"), std::string::npos);
    EXPECT_NE(vertex.find("vColor0.rgb *= ubo.uPrimColor.rgb"), std::string::npos);
    // `lit` must not gate the modulation. `lit` is "apply the character/prop lighting term";
    // ZELDA3D_HANDLE_FORCE_UNLIT clears it while leaving the modulation intact, so requiring it
    // drops the modulation for every force-unlit and every scene-geometry draw. This assertion is
    // the regression lock: it fails on the gate that rendered the title fire-glow white instead
    // of the oracle's amber.
    EXPECT_EQ(vertex.find("ubo.uParams1.x"), std::string::npos)
        << "uParams1.x (the N64 noise scale) must not gate the CMB draw modulation";
    const auto alphaTest = fragment.find("if (afn > 0 && !alphaPass");
    const auto drawAlpha = fragment.find("texel.a *= ubo.uPrimColor.a");
    ASSERT_NE(alphaTest, std::string::npos);
    ASSERT_NE(drawAlpha, std::string::npos);
    EXPECT_LT(alphaTest, drawAlpha);
}

TEST(Zelda3DUnifiedUbo, CmbDrawModulationCarriesCallerRgbaWithNoEnableSwitch) {
    Zelda3DUnified::CommonUbo unified{};
    Zelda3DUnified::PackCmbDrawModulation(unified, 64, 128, 192, 32);
    EXPECT_FLOAT_EQ(unified.uPrimColor[0], 64.0f / 255.0f);
    EXPECT_FLOAT_EQ(unified.uPrimColor[1], 128.0f / 255.0f);
    EXPECT_FLOAT_EQ(unified.uPrimColor[2], 192.0f / 255.0f);
    EXPECT_FLOAT_EQ(unified.uPrimColor[3], 32.0f / 255.0f);
    // Written deterministically, not left to carry a stale N64-noise-scale meaning.
    EXPECT_FLOAT_EQ(unified.uParams1[0], 0.0f);
}

TEST(Zelda3DUnifiedUbo, ForceUnlitDrawKeepsItsRgbModulation) {
    // The title fire-glow's own numbers, sampled live at cs=1093 from g_title_fire.cmab's
    // ConstColor channel 0 via the `log fireglow` channel: rgb=(0.8000,0.4300,0.0000). A
    // force-unlit draw (lit == 0) must still carry them; the measured symptom of losing them was
    // an all-three-channel-saturated white glow (1 : 1.00 : 0.99) against the oracle's amber
    // (1 : 0.93 : 0.58) over the same pixels.
    const auto byte = [](float v) { return static_cast<uint8_t>(v * 255.0f + 0.5f); };
    Zelda3DUnified::CommonUbo unified{};
    Zelda3DUnified::PackCmbDrawModulation(unified, byte(0.80f), byte(0.43f), byte(0.00f), 255);
    EXPECT_FLOAT_EQ(unified.uPrimColor[0], byte(0.80f) / 255.0f);
    EXPECT_FLOAT_EQ(unified.uPrimColor[1], byte(0.43f) / 255.0f);
    EXPECT_FLOAT_EQ(unified.uPrimColor[2], 0.0f);
    EXPECT_FLOAT_EQ(unified.uPrimColor[3], 1.0f);
    // The blue channel is the discriminator: the oracle's glow keeps blue at 0.58 of red, so a
    // dropped tint (which would leave blue at full) is a visible error, not a rounding difference.
    EXPECT_LT(unified.uPrimColor[2], unified.uPrimColor[0] * 0.6f);
}

TEST(Zelda3DUnifiedUbo, CmbLightBankPreservesAmbientMultiplicityAndBothSlots) {
    SgUbo native{};
    native.uAmbient[0] = 0.2f;
    native.uAmbient[1] = 0.3f;
    native.uAmbient[2] = 0.4f;
    native.uAmbient[3] = 2.0f;
    for (int component = 0; component < 4; ++component) {
        native.uMatDiffuse[component] = 0.1f + component;
        native.uPrimaryCtl[component] = 0.5f + component;
        native.uLightDir[component] = 10.0f + component;
        native.uLitDif1[component] = 20.0f + component;
        native.uLitDif2[component] = 30.0f + component;
        native.uLightDir2[component] = 40.0f + component;
    }

    Zelda3DUnified::CommonUbo unified{};
    Zelda3DUnified::CopyCmbVertexLightBank(unified, native);

    EXPECT_FLOAT_EQ(unified.uMatAmbient[0], 0.4f);
    EXPECT_FLOAT_EQ(unified.uMatAmbient[1], 0.6f);
    EXPECT_FLOAT_EQ(unified.uMatAmbient[2], 0.8f);
    for (int component = 0; component < 4; ++component) {
        EXPECT_FLOAT_EQ(unified.uMatDiffuse[component], native.uMatDiffuse[component]);
        EXPECT_FLOAT_EQ(unified.uPrimaryCtl[component], native.uPrimaryCtl[component]);
        EXPECT_FLOAT_EQ(unified.uLightDir[component], native.uLightDir[component]);
        EXPECT_FLOAT_EQ(unified.uLitDif1[component], native.uLitDif1[component]);
        EXPECT_FLOAT_EQ(unified.uLitDif2[component], native.uLitDif2[component]);
        EXPECT_FLOAT_EQ(unified.uLightDir2[component], native.uLightDir2[component]);
    }
}

// The unified draw packer used to copy the fog PARAMETERS (uFog3d0/uFog3d1) and no gate, so the
// shader's fog block — which is gated on the mode — evaluated to nothing for every unified draw.
// The gate copy is the one line that decides it, so it gets a seam and a test of its own rather
// than living unexercised inside the draw loop.
TEST(Zelda3DUnifiedUbo, FogGateCarriesTheNativeMode) {
    Zelda3DSg::SgUbo native{};
    native.uFog[0] = 0.9f;
    native.uFog[1] = 0.8f;
    native.uFog[2] = 0.7f;
    // 2.0 = the PICA distance-fog LUT, set by zelda3d_sdl3gpu_pass.cpp when
    // (gZelda3dFog3dOn && grp.fogEnabled).
    native.uFog[3] = 2.0f;

    Zelda3DUnified::CommonUbo unified{};
    // Pre-poison every lane: a packer that copied the mode nowhere would leave the sentinel.
    for (int component = 0; component < 4; ++component) {
        unified.uFogCtl[component] = -1.0f;
    }

    Zelda3DUnified::PackCmbFogGate(unified, native);

    EXPECT_FLOAT_EQ(unified.uFogCtl[0], 2.0f);
    // The remaining lanes are not part of the enum; they must be zeroed, not left as whatever the
    // draw loop's reused UBO last held, because a stale lane here is a future field nobody set.
    EXPECT_FLOAT_EQ(unified.uFogCtl[1], 0.0f);
    EXPECT_FLOAT_EQ(unified.uFogCtl[2], 0.0f);
    EXPECT_FLOAT_EQ(unified.uFogCtl[3], 0.0f);
}

// The per-draw texcoord SCROLL is a per-DRAW input the unified route dropped: it carried the
// per-group coordinator transform (uTex0Xf) but not the draw's own scroll, so every scrolling draw
// sat still. Measured live at title cs=1093, 2 of 101 draws carry a non-zero scroll — both
// (0.57222, 0), the OoT3D sky cloud band's .cmab rate. Lock the term into BOTH non-sphere tex0
// branches, because a draw that lands on the variant without it would lose the scroll purely
// through classification.
TEST(UnifiedShader, ThePerDrawUvScrollIsAddedOnEveryNonSphereTex0Path) {
    for (int index = 0; index < static_cast<int>(Fast::Unified::Variant::kCount); ++index) {
        const auto variant = static_cast<Fast::Unified::Variant>(index);
        const std::string vertex = Fast::Unified::BuildVertexSource(variant);
        const std::string label = Fast::Unified::VariantName(variant);
        // The sphere-mapped branch is the one place the scroll must NOT appear: the native derives
        // that UV from the normal and the coordinator matrix, and a sphere has no texcoord to
        // scroll. Exactly one of the two plain forms must be present per variant.
        const bool flipped =
            vertex.find("vUv0 = vec2(aUv0.x + ubo.uUvScroll.x, 1.0 - aUv0.y + ubo.uUvScroll.y);") != std::string::npos;
        const bool unflipped =
            vertex.find("vUv0 = vec2(aUv0.x + ubo.uUvScroll.x, aUv0.y + ubo.uUvScroll.y);") != std::string::npos;
        EXPECT_TRUE(flipped != unflipped)
            << label << ": expected exactly one plain tex0 form, got " << (flipped ? "the flipped one" : "nothing")
            << (flipped && unflipped ? " AND the unflipped one" : "");
        // The native adds the scroll AFTER the flip (it writes `1.0 - aUv.y + uExtra.z`), so a
        // pre-flip subtract would land the same value by accident on a symmetric UV and be wrong
        // everywhere else. The term must be an add on the already-flipped coordinate.
        EXPECT_EQ(vertex.find("1.0 - aUv0.y - ubo.uUvScroll.y"), std::string::npos) << label;
    }
}

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
    EXPECT_EQ(offsetof(SgUbo, uMatDiffuse), 320u);
    EXPECT_EQ(offsetof(SgUbo, uPrimaryCtl), 336u);
    EXPECT_EQ(offsetof(SgUbo, uMatConst), 352u);
    EXPECT_EQ(offsetof(SgUbo, uSheen), 368u);
    EXPECT_EQ(offsetof(SgUbo, uTex0Xf), 384u);
    EXPECT_EQ(offsetof(SgUbo, uTex1Xf), 400u);
    EXPECT_EQ(offsetof(SgUbo, uFog3d0), 416u);
    EXPECT_EQ(offsetof(SgUbo, uFog3d1), 432u);
    EXPECT_EQ(offsetof(SgUbo, uSphNrm0), 448u);
    EXPECT_EQ(offsetof(SgUbo, uSphNrm1), 464u);
    EXPECT_EQ(offsetof(SgUbo, uSphNrm2), 480u);
    EXPECT_EQ(offsetof(SgUbo, uLitDif1), 496u);
    EXPECT_EQ(offsetof(SgUbo, uLitDif2), 512u);
    EXPECT_EQ(offsetof(SgUbo, uLightDir2), 528u);
    // Generic per-stage TEV (render.multi-stage-tev): uvec4[6] + uvec4[2] + vec4 + vec4.
    // std140 array stride of uvec4 is 16 bytes, so the flat uint32_t arrays match exactly.
    EXPECT_EQ(offsetof(SgUbo, uTevStages), 544u);
    EXPECT_EQ(offsetof(SgUbo, uTevConst), 640u);
    EXPECT_EQ(offsetof(SgUbo, uTex2Xf), 672u);
    EXPECT_EQ(offsetof(SgUbo, uTevCtl), 688u);
    EXPECT_EQ(offsetof(SgUbo, uDebug), 704u);
    EXPECT_EQ(offsetof(SgUbo, uBones), 720u);
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

// The sampler count SDL3 is told about becomes the shader's bind-group layout, so a count smaller
// than the generated GLSL's sampler declarations builds a layout the SPIR-V reads past. That is not a
// validation error, and it faulted inside pipeline creation on the title wordmark's dual-texture
// draw (issue #25). Assert the declared count against the generated source itself, so the two
// cannot drift again the way a hand-maintained table did.
TEST(UnifiedShader, DeclaredSamplerCountMatchesGeneratedBindings) {
    for (int index = 0; index < static_cast<int>(Fast::Unified::Variant::kCount); ++index) {
        const auto variant = static_cast<Fast::Unified::Variant>(index);
        const std::string fragment = Fast::Unified::BuildFragmentSource(variant, 0);
        std::size_t declared = 0;
        for (std::size_t at = fragment.find("uniform sampler2D"); at != std::string::npos;
             at = fragment.find("uniform sampler2D", at + 1)) {
            ++declared;
        }
        EXPECT_EQ(static_cast<uint32_t>(declared), Fast::Unified::VariantSamplerCount(variant))
            << "variant " << index << " (" << Fast::Unified::VariantName(variant) << "): declared "
            << Fast::Unified::VariantSamplerCount(variant) << " samplers but the "
            << "generated fragment stage binds " << declared;
    }
}

// The OoT3D PICA distance fog (uFogCtl.x == 2) is a SEPARATE mode from the N64 ramp above, and it
// lives in the stage after the combiner, so every variant a CMB draw can land on must carry it.
// The unified route compiled no fog at all until now: the packer copied uFog3d0/uFog3d1 (frame-level
// parameters) but no per-draw GATE, so every draw evaluated the block to nothing. Measured at title
// cs=1093 on the route that does have it, the fog is worth 9.63 mean-abs against the oracle
// (43.56 with, 53.19 without), so this was not a cosmetic omission.
TEST(UnifiedShader, PicaDistanceFogIsInEveryVariant) {
    for (int index = 0; index < static_cast<int>(Fast::Unified::Variant::kCount); ++index) {
        const auto variant = static_cast<Fast::Unified::Variant>(index);
        const std::string fragment = Fast::Unified::BuildFragmentSource(variant, 0);
        const std::string label = Fast::Unified::VariantName(variant);
        EXPECT_NE(fragment.find("float fog3dNode(float t)"), std::string::npos) << label;
        EXPECT_NE(fragment.find("if (ubo.uFogCtl.x > 1.5 && ubo.uLightDir[3] < 0.5)"), std::string::npos) << label;
    }
}

// The mix argument order is the whole fog. mix(texel, fogColor, factor) is "fog the surface"; the
// native path's mix(uFog.xyz, rgb, factor) is "fog toward the colour by the surviving factor".
// Swapping them is a one-token change that compiles, passes every structural test, and inverts the
// haze — the draw gets MORE distant-looking as it approaches, which reads as a subtler colour bug.
TEST(UnifiedShader, PicaFogMixesTowardTheFogColourByTheSurvivingFactor) {
    const std::string fragment = Fast::Unified::BuildFragmentSource(Fast::Unified::Variant::kGenericTev, 0);
    EXPECT_NE(fragment.find("texel.rgb = mix(ubo.uFogColor.rgb, texel.rgb, factor);"), std::string::npos);
    EXPECT_EQ(fragment.find("mix(texel.rgb, ubo.uFogColor.rgb, factor)"), std::string::npos);
    // ...and factor == 1 must mean "unfogged", which is what puts the SURFACE in the second slot.
    EXPECT_NE(fragment.find("return 1.0;"), std::string::npos); // nearer than fogNear
    EXPECT_NE(fragment.find("return 0.0;"), std::string::npos); // beyond fogFar
}

// The 3DS indexes its 128-entry fog LUT by the fragment's z-buffer DEPTH, recovered from the
// interpolated world position as a - b/d. A per-vertex fog factor is NOT equivalent: two variants of
// that were measured and falsified on the native path, so this locks the depth form and the
// in-entry interpolation (which is the visible haze under a compressed depth range).
TEST(UnifiedShader, PicaFogUsesPerFragmentDepthAndThe128EntryLutInterpolation) {
    const std::string fragment = Fast::Unified::BuildFragmentSource(Fast::Unified::Variant::kGenericTev, 0);
    EXPECT_NE(fragment.find("float d3 = dot(vWorld, ubo.uFog3d1.xyz) - ubo.uFog3d1.w;"), std::string::npos);
    EXPECT_NE(fragment.find("float depth3ds = ubo.uFog3d0.x - ubo.uFog3d0.y / max(d3, 1e-3);"), std::string::npos);
    EXPECT_NE(fragment.find("float x = clamp(depth3ds, 0.0, 1.0) * 128.0;"), std::string::npos);
    EXPECT_NE(fragment.find("float i0 = min(floor(x), 127.0);"), std::string::npos);
    EXPECT_NE(fragment.find("float factor = clamp(f0 + (f1 - f0) * (x - i0), 0.0, 1.0);"), std::string::npos);
}

// A varying that is declared in the fragment stage but never written in the vertex stage is not a
// compile error in every toolchain and is a silently always-zero fog where it is. Both halves, on
// every variant, matched by location so a renumbering cannot hide a mismatch.
TEST(UnifiedShader, TheFogWorldVaryingIsDeclaredOnBothStagesAtTheSameLocation) {
    for (int index = 0; index < static_cast<int>(Fast::Unified::Variant::kCount); ++index) {
        const auto variant = static_cast<Fast::Unified::Variant>(index);
        const std::string label = Fast::Unified::VariantName(variant);
        const std::string vertex = Fast::Unified::BuildVertexSource(variant);
        const std::string fragment = Fast::Unified::BuildFragmentSource(variant, 0);
        EXPECT_NE(vertex.find("layout(location=10) out vec3 vWorld;"), std::string::npos) << label;
        EXPECT_NE(fragment.find("layout(location=10) in vec3 vWorld;"), std::string::npos) << label;
        EXPECT_NE(vertex.find("vWorld = (ubo.uMv * vec4(sp, 1.0)).xyz;"), std::string::npos) << label;
    }
}

// The mode enum is shared with the native route, not re-derived here: one number moves a draw
// between the PICA LUT and the F3DEX ramp on either route. This is the property that stopped the
// unified route silently dropping the fog (it copied the parameters, never the gate).
TEST(UnifiedShader, TheFogModeEnumIsTheNativeOne) {
    const std::string fragment = Fast::Unified::BuildFragmentSource(Fast::Unified::Variant::kGenericTev, 0);
    // The N64 ramp is selected by its own compiled feature (@if(o_fog)), not by the mode enum, so a
    // 0/1 test on uFogCtl.x would be a SECOND policy rather than a read of the native one. Only the
    // 2.0 test is allowed to exist.
    EXPECT_EQ(fragment.find("ubo.uFogCtl.x > 0.5"), std::string::npos);
    // The N64 ramp block itself is only compiled into the variant that selects it (@if(o_fog), set
    // for kDualTexFog), so read it there — checking a variant without it would be vacuous.
    const std::string n64Ramp = Fast::Unified::BuildFragmentSource(Fast::Unified::Variant::kDualTexFog, 0);
    EXPECT_NE(n64Ramp.find("texel.rgb = mix(texel.rgb, ubo.uFogColor.rgb, clamp(vFog.x, 0.0, 1.0));"),
              std::string::npos);
}

// The two-sampler variants are the ones that were under-declared (they were told 1). Pin them
// explicitly so a future edit to the feature table cannot quietly drop them back.
// A dual-texture variant does NOT evaluate the combiner at all: the generated fragment stage
// supersedes the N64-shaped uCombA program with the three byte-classified PICA dual-texture shapes
// (kDualTexAddMult / kDualTexAddThenModulatePrimary / kDualTexModulateThenScale) selected by
// uSheen.y. So whatever the host packs into uCombA is DEAD for those variants. This is the check
// that keeps a host-side placeholder from being mistaken for the mechanism a dual-texture draw
// actually uses.
TEST(UnifiedShader, DualTextureVariantsUseClassifiedShapesNotTheCombMuxProgram) {
    const std::string dualTex = Fast::Unified::BuildFragmentSource(Fast::Unified::Variant::kDualTex, 0);
    const std::string singleTex = Fast::Unified::BuildFragmentSource(Fast::Unified::Variant::kSingleTex, 0);

    const auto mainBody = [](const std::string& source) {
        const std::size_t at = source.find("void main()");
        EXPECT_NE(at, std::string::npos);
        return source.substr(at);
    };

    // The three shapes, keyed on the classified mode the host packs into uSheen.y.
    EXPECT_NE(dualTex.find("ubo.uSheen.y > 2.5"), std::string::npos) << "kDualTex lost the scale2 shape";
    EXPECT_NE(dualTex.find("ubo.uSheen.y > 1.5"), std::string::npos) << "kDualTex lost the add-then-modulate shape";
    EXPECT_NE(dualTex.find("clamp(t0.rgb + t1, 0.0, 1.0) * t0.rgb"), std::string::npos)
        << "kDualTex lost the ADD_MULT shape";

    EXPECT_EQ(mainBody(dualTex).find("evalCycle("), std::string::npos)
        << "kDualTex reached the uCombA program, so the host's uCombA is not dead for it";
    EXPECT_NE(mainBody(singleTex).find("evalCycle("), std::string::npos)
        << "kSingleTex stopped evaluating its combiner";
}

TEST(UnifiedShader, DualTextureVariantsDeclareTwoSamplers) {
    EXPECT_EQ(Fast::Unified::VariantSamplerCount(Fast::Unified::Variant::kDualTex), 2u);
    EXPECT_EQ(Fast::Unified::VariantSamplerCount(Fast::Unified::Variant::kDualTexFog), 2u);
    EXPECT_EQ(Fast::Unified::VariantSamplerCount(Fast::Unified::Variant::kGenericTev), 3u);
    EXPECT_EQ(Fast::Unified::VariantSamplerCount(Fast::Unified::Variant::kSingleTex), 1u);
    EXPECT_EQ(Fast::Unified::VariantSamplerCount(Fast::Unified::Variant::kUntextured), 0u);
}
