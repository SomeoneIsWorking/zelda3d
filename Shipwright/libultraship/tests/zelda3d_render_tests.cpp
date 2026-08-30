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

TEST(Zelda3DUnifiedShader, CmbDrawModulationPreservesTintGateAndPostTevAlpha) {
    const std::string vertex = Fast::Unified::BuildVertexSource(Fast::Unified::Variant::kGenericTev);
    const std::string fragment = Fast::Unified::BuildFragmentSource(Fast::Unified::Variant::kGenericTev);
    EXPECT_NE(vertex.find("ubo.uParams1.w < 0.5 && ubo.uParams1.x > 0.5"), std::string::npos);
    EXPECT_NE(vertex.find("vColor0.rgb *= ubo.uPrimColor.rgb"), std::string::npos);
    const auto alphaTest = fragment.find("if (afn > 0 && !alphaPass");
    const auto drawAlpha = fragment.find("texel.a *= ubo.uPrimColor.a");
    ASSERT_NE(alphaTest, std::string::npos);
    ASSERT_NE(drawAlpha, std::string::npos);
    EXPECT_LT(alphaTest, drawAlpha);
}

TEST(Zelda3DUnifiedUbo, CmbDrawModulationUsesCallerRgbaAndLitGate) {
    Zelda3DUnified::CommonUbo unified{};
    Zelda3DUnified::PackCmbDrawModulation(unified, 64, 128, 192, 32, true);
    EXPECT_FLOAT_EQ(unified.uPrimColor[0], 64.0f / 255.0f);
    EXPECT_FLOAT_EQ(unified.uPrimColor[1], 128.0f / 255.0f);
    EXPECT_FLOAT_EQ(unified.uPrimColor[2], 192.0f / 255.0f);
    EXPECT_FLOAT_EQ(unified.uPrimColor[3], 32.0f / 255.0f);
    EXPECT_FLOAT_EQ(unified.uParams1[0], 1.0f);
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
