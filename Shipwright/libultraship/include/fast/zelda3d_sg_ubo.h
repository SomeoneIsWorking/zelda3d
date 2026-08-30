#ifndef ZELDA3D_SG_UBO_H
#define ZELDA3D_SG_UBO_H

// Zelda3D SDL3 GPU per-draw uniform layout — the single source of truth shared by the renderer pass
// and shader owners and the layout unit tests (tests/zelda3d_render_tests.cpp).
//
// WHY THIS IS A HEADER WITH HARD INVARIANTS: SDL3 GPU's Vulkan backend binds every *pushed* uniform
// block with a descriptor range capped at MAX_UBO_SECTION_SIZE = 4096 bytes. Any shader field past
// offset 4096 reads OUTSIDE the bound range and returns 0. A single combined UBO (uMP + uMV + 64
// bone mat4s + lighting/fog/...) is 4416 bytes, so everything after the bones array — uLightDir,
// uParams, uTintSkin, uExtra, lighting, fog, ambient — silently read 0, producing a BLACK world and
// T-posed characters (skin-enable lives in uTintSkin.w). The fix splits the data into two pushed
// blocks, each <= 4096: COMMON (the small per-draw state) and BONES (the matrix array). The
// static_asserts below make a future overflow a COMPILE error instead of a silent black scene.

#include <cstddef>
#include <cstdint>
#include "fast/zelda3d_model_types.h"

namespace Zelda3DSg {

// SDL3 GPU (Vulkan backend) MAX_UBO_SECTION_SIZE: the per-pushed-block descriptor range cap.
constexpr uint32_t kMaxUboSectionBytes = 4096;

// std140 UBO layout. The COMMON fields come first (pushed as one block at binding 0, both stages);
// uBones is LAST so it forms a separate contiguous block pushed to vertex binding 1. Every field is
// vec4/mat4 (16-byte aligned), so the C offsets equal the std140 offsets the shader computes — the
// offset asserts below lock that correspondence so a field reorder can't silently desync the two.
struct SgUbo {
    float uMP[16];
    float uMV[16];
    float uLightDir[4];
    float uParams[4];
    float uTintSkin[4];
    float uExtra[4];
    float uLightVP[16];
    float uShadow[4];
    float uFog[4];
    float uFog2[4];
    float uAmbient[4];
    // PICA200 TEV constant-color: the selected slot (matConstant[combConstIdx]) for this
    // group's stage-0 combiner. .rgb = the color; .a = APPLY FLAG (>=0.5 modulates the
    // fragment output; <0.5 is a no-op so materials that don't use CONSTANT are unchanged).
    // Default upload sets a=0 (no-op preserving today's rendering); the per-actor override
    // channel (EnHy Step 2c) flips a=1 when an actor wants the constant applied. See
    // debug_journal/2026-07-02-en-hy-body-colors.md.
    float uMatConst[4];
    // Wordmark "sheen" — the CmbVShader vertex-lit color term for a per-draw light-direction
    // override (title wordmark, title_logo_actor.md §6.3/§6.6: actor field +0x1DC sweeps the
    // light-env slot-0 DIRECTION across the wordmark's own material; light ambient={0.18,...},
    // light diffuse=WHITE {1,1,1,1} — byte-exact from code.bin pool 0x004d9924 AND read back from
    // the oracle's live c81/c82 vertex uniforms at the wordmark draw, 2026-07-10). .x = the light
    // AMBIENT coefficient (0 = no override; 0.18 when a draw sets a light-dir override — doubles
    // as the gate); .y/.z used by the dual-texture path; .w carries coordinator-0's mapping
    // method. The shader does
    //   shade *= clamp(uSheen.x + max(0, dot(N, -L)), 0, 1)
    // i.e. the faithful  matAmb*lightAmb + max(0,N·(-L))*matDif*lightDif  with both material
    // colors white, matching PICA's dp3 against the NEGATED c80 light dir. (A previous port used
    // shade *= 1 + 0.1834*max(0,N·(+L)) — slot colors swapped AND light-dir sign flipped; it
    // measured bit-flat while the oracle brightens x1.40 across the ramp. Falsified 2026-07-10.)
    // Specular is NOT ported: the vertex-lit CmbVShader path has no specular term at all
    // (oot3d-decomp title_env_lighting.md §10 disassembly — uniform table has only
    // dir/diffuse/ambient per light), so there is nothing to port for this draw class.
    float uSheen[4];
    // Coordinator-0 scale/translation for the primary sampler. This is live when mapping method
    // 3 selects CameraSphereEnvMap (title_logo_us mats 10/11); ordinary UV materials retain the
    // established aUv + per-draw offset path.
    float uTex0Xf[4];
    // Dual-texture stage 0 (PICA ADD_MULT detail mask, g_title.cmb fire-glow —
    // oot3d-decomp/docs/title_logo_fireglow_cmab.md §3.1): coordinator-1 UV transform for the
    // second sampler (uTex1). .xy = coordinator scale (S,T), .zw = coordinator translate (S,T)
    // PLUS any per-draw CMAB UV-scroll routed to coordinator 1. The shader computes
    // uv1 = scale * (uv - trans) (noclip calcTexMtx, rot=0) and samples uTex1 with it.
    // The dual-tex ENABLE flag rides uSheen.y (uSheen.z is its scale); when off, uTex1Xf
    // is dead data and uTex1 is bound to the dummy texture.
    float uTex1Xf[4];
    // OoT3D PICA distance fog (title port — oot3d-decomp docs/title_env_lighting.md §13, the
    // RE'd FogResUpdater LUT fill). Active per-draw when uFog.w == 2.0 (the material's CMB
    // isFogEnabled byte + the frame-level enable, never sky).
    //   uFog3d0 = (a, b, fogNear, fogFar): a/b are the 3DS projection z-row coefficients
    //             (eyeDist at LUT node t = b/(a - t)); fogNear/fogFar the palette-blended
    //             linear fog window in eye units.
    //   uFog3d1 = (fwd.xyz, dot(fwd, eye)): camera forward + eye projection, so the vertex
    //             shader recovers eye depth d = dot(worldPos, fwd) - w without a view matrix
    //             (the camera lives folded into uMP for scene draws).
    float uFog3d0[4];
    float uFog3d1[4];
    // Sphere-map (CameraSphereEnvMap) VIEW-rotation override — rows of the live camera's
    // view-rotation matrix, for draws whose model-view matrix does NOT carry the camera (the
    // title 2D ortho-overlay pass: uMV there is a fixed screen placement, so mat3(uMV)*n is NOT
    // a view-space normal). On the 3DS the sphere-map unit consumes the VIEW-space normal
    // (CmbVShader; the wordmark decorations' placement rotation is identity per
    // title_logo_actor.md §6.1, so n_view = R_view * n_model exactly). uSphRot0/1/2.xyz = the
    // LH view basis ROWS (right / up / forward=normalize(eye-at), the FUN_002d9e68 convention —
    // oot3d-decomp/docs/title_view_matrix_lh.md); uSphRot0.w = enable flag (>=0.5). When off
    // (default 0) the shader keeps the mat3(uMV) path, so every non-overlay draw is unchanged.
    float uSphRot0[4];
    float uSphRot1[4];
    float uSphRot2[4];
    // OoT3D CmbVShader per-light DIFFUSE terms (#153/#111 — oot3d-decomp title_env_lighting.md
    // §10 disassembly). The PICA vertex-lit program computes, per enabled light i:
    //   o1 += matAmbient*LightAmbientColor_i + max(0, dot(N, -LightDir_i))*matDiffuse*LightDiffuseColor_i
    // uAmbient already carries the ambient half (matAmb*sceneAmb, summed via uAmbient.w).
    //   uLitDif1.rgb = matDiffuse * sceneLight1Color   (light slot 1's diffuse product)
    //   uLitDif2.rgb = matDiffuse * sceneLight2Color   (light slot 2's diffuse product)
    //   uLightDir2.xyz = light2 world direction (uLightDir carries light1); both normalized
    //   CPU-side, or (0,0,0) when the slot's direction is degenerate — a zero dir nulls only
    //   that light's DIFFUSE term (FUN_003fa5d0 semantics), never its ambient contribution.
    float uLitDif1[4];
    float uLitDif2[4];
    float uLightDir2[4];
    // Generic per-stage PICA TEV chain (render.multi-stage-tev). Active when uTevCtl.x > 0
    // (= stage count); the legacy combiner fields (uExtra.w scale, uMatConst, uSheen.y dual
    // modes) are then bypassed — per-stage ops/sources/operands/scales/const-selects are
    // decoded from the packed words instead (shader mirrors: uvec4 uTevStages[6],
    // uvec4 uTevConst[2], vec4 uTex2Xf, vec4 uTevCtl).
    //   uTevStages[s] = { w0 srcs, w1 mods+constIdx, w2 ops+scales, 0 } — packing documented
    //     at Zelda3DGlGroup::tevStagePack (zelda3d_model_types.h).
    //   uTevConst = 6 RGBA8-packed constant-color slots (PICA's const registers are 8-bit) in
    //     words [0..5], AFTER the per-actor override channel is applied.
    //   uTex2Xf = coordinator-2 scale (xy) / translate (zw) for the third texture unit.
    //   uTevCtl = { stageCount, coord1Mapping, coord2Mapping, 0 }.
    uint32_t uTevStages[6 * 4];
    uint32_t uTevConst[8];
    float uTex2Xf[4];
    float uTevCtl[4];
    // Renderer-only probe state. uDebug.x is 1 for the selected draw when
    // ZELDA3D_SG_FRAGDBG_DRAW is active, otherwise 0. It lets a fragment tap
    // retain the normal render-pass context around the selected draw.
    float uDebug[4];
    float uBones[ZELDA3D_GL_MAX_BONES * 16]; // MUST stay last: pushed as its own <=4096 B block
};

// Byte ranges of the two pushed blocks. Both MUST be <= kMaxUboSectionBytes.
constexpr uint32_t kCommonBytes = (uint32_t)offsetof(SgUbo, uBones);
constexpr uint32_t kBonesBytes = (uint32_t)(sizeof(SgUbo) - offsetof(SgUbo, uBones));

static_assert(kCommonBytes <= kMaxUboSectionBytes,
              "Zelda3D common UBO block exceeds SDL3 GPU's 4096-byte push cap; split it further");
static_assert(kBonesBytes <= kMaxUboSectionBytes,
              "Zelda3D bone UBO block exceeds SDL3 GPU's 4096-byte push cap; move bones to a "
              "read-only storage buffer rather than enlarging the push");

// Front-face winding for OoT3D geometry. OoT3D winds its front faces CCW from the geometric normal.
// The sole backend (SDL3 GPU) renders fb0 WITHOUT a clip-Y negation, so the rasterizer sees that CCW
// directly: front-face = CCW unless the empirical `facecull` override flips it. `faceCullFlip` is
// gZelda3dFaceCullFlip (default 0). Returns true if the winding should be CLOCKWISE.
inline bool FrontFaceIsCW(int faceCullFlip) {
    return faceCullFlip != 0;
}

} // namespace Zelda3DSg

#endif // ZELDA3D_SG_UBO_H
