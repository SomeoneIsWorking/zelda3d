#ifndef ZELDA3D_SG_UBO_H
#define ZELDA3D_SG_UBO_H

// Zelda3D SDL3 GPU per-draw uniform layout — the single source of truth shared by the renderer
// (zelda3d_sdl3gpu.cpp) and the layout unit tests (tests/zelda3d_render_tests.cpp).
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
#include "fast/zelda3d_gl.h" // ZELDA3D_GL_MAX_BONES — shared by the shader string, this struct, the tests

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
    // Additive diffuse "sheen" boost for a per-draw light-direction override (title wordmark,
    // title_logo_actor.md §6.3: actor field +0x1DC sweeps a fragment-light DIRECTION across the
    // wordmark's own material — decompiled ambient={1,1,1,1} diffuse={0.1834,...} specular={1,1,1,1}
    // emission=0, direction the only animated part). .x = diffuse strength (0 = no override / no
    // boost, 0.1834 = the decompiled constant when a draw sets a light-dir override); .y/.z/.w
    // unused (reserved, always 0). The shader does shade *= (1.0 + uSheen.x * max(0,N.L)), i.e. an
    // ADDITIVE brightening on top of the existing full-bright tint — NOT the shared uParams.y
    // half-Lambert term (that one DARKENS via a 0.55..1.0 multiplier, which is not what the
    // decomp's ambient=1-always + small diffuse bonus reduces to). Specular is NOT ported: the
    // PICA specular exponent isn't in the actor's own code (title_logo_actor.md §6.2/§6.3 — it's a
    // CMB-material-side LUT config this decomp pass didn't need to touch), and this ortho overlay
    // pass has no real camera/view vector for a Blinn-Phong H term to reduce to — proven-negative,
    // not guessed at.
    float uSheen[4];
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
