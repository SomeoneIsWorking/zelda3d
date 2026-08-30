#pragma once

#include <cstdint>

// Render-unification effort (kanban #131, plan "Full N64/3DS Render Pipeline Merge").
//
// UnifiedVtx is the ONE vertex layout used by the unified N64 Fast3D combiner pipeline and 3DS CMB
// model pipeline. It is a strict superset of the two pipelines' per-vertex data:
//   - N64 (LoadedVertex, interpreter.h) writes pos/uv0/color0 (CPU-baked Gouraud shade) and leaves
//     texClamp/color1-3/uv1/uv2/fog/bone* at their identity defaults (boneW = {255,0,0,0} = 100% bone 0).
//   - 3DS (CmbVertex, cmb3d/asset/cmb.h) writes pos/nrm/uv0/uv1/uv2/color0/boneIds/boneW and leaves
//     texClamp/color1-3/fog at their identity defaults.
//
// Both emitters now populate it when their gUnifiedRenderer bit is active. The two skinning
// MECHANISMS themselves are explicitly NOT unified (N64 stays CPU limb-walk writing identity bone
// data; 3DS stays GPU 4-bone blend) — see the plan's "Explicit non-goal".
struct UnifiedVtx {
    // xyz = model-space (3DS) or ALREADY-CLIP-SPACE (N64 — GfxSpVertex CPU-transforms via the
    // emulated RSP pipeline, guard-band clipping included; there is no clean model-space for N64
    // content to re-derive). w = 1.0 (3DS) or the real perspective w (N64) — needed for correct
    // perspective interpolation, so pos must carry all 4 components, not assume w=1.
    // UnifiedMaterial.alreadyTransformed picks which: true (N64) -> gl_Position = pos verbatim;
    // false (3DS) -> gl_Position = uMvp * vec4(pos.xyz, 1.0).
    float pos[4];
    float nrm[3];       // vertex normal; N64 content that has no real normal writes {0,0,1}
    float uv0[2];       // primary coordinate (N64 texel0; CMB coordinator-0 selected source)
    float uv1[2];       // secondary coordinate (N64 texel1; CMB coordinator-1 selected source)
    float texClamp[4];  // {clampS0, clampT0, clampS1, clampT1} — mirrors N64's per-vertex
                        // aTexClampS/T attributes (gfx_sdl3gpu.cpp's o_clamp path); 3DS content
                        // that doesn't clamp writes the tex's full extent (no-op clamp).
    uint8_t color0[4];  // RGBA. N64: CPU-baked Gouraud shade (SHADER_INPUT_1 in the combiner).
                        // 3DS: per-vertex color (tinting); combined with lightingMode in the
                        // material, not baked at emit time like N64's.
    uint8_t color1[4];  // combiner SHADER_INPUT_2 slot (N64 prim/env/lod-frac source data that's
                        // genuinely per-vertex rather than per-draw-uniform); zeroed otherwise.
    uint8_t color2[4];  // combiner SHADER_INPUT_3 slot; zeroed otherwise.
    uint8_t color3[4];  // combiner SHADER_INPUT_4 slot; zeroed otherwise.
    float fog[2];       // {fogAmount, fogAlpha} — N64 fog blend factor; 3DS writes {0,0} (no fog).
    uint8_t boneIds[4]; // 3DS: up to 4 skin-matrix indices. N64: {0,0,0,0} (identity/no skin).
    uint8_t boneW[4];   // 3DS: per-bone blend weights (unorm8, sum to 255). N64: {255,0,0,0}.
    float uv2[2];       // CMB coordinator-2 selected source; N64 writes {0,0}
};

static_assert(sizeof(UnifiedVtx) == 100, "UnifiedVtx layout changed — update the emitters in lockstep");
